#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <signal.h>
#include <poll.h>
#include <errno.h>

#include <metal/sys.h>
#include <metal/device.h>
#include <metal/alloc.h>
#include <metal/io.h>
#include <openamp/open_amp.h>
#include <openamp/rpmsg_virtio.h>

#define VRING0_ADDR 0x3ee00000
#define VRING1_ADDR 0x3ee04000
#define BUF_ADDR    0x3ee08000
#define SHM_SIZE    0x30000  /* 192KB total SHM size mapped at 0x3ee00000 (including IPI TRIG at offset 0x2c000) */

#define VRING_SIZE  16
#define VRING_ALIGN 4096

static struct rpmsg_virtio_device rvdev;
static struct rpmsg_endpoint ept;
static volatile sig_atomic_t sigusr1_fired = 0;
static int running = 1;

static void sigusr1_handler(int sig) {
    (void)sig;
    sigusr1_fired = 1;
    const char *msg = "[M-Core] SIGUSR1 signal fired in handler\n";
    write(1, msg, strlen(msg));
}

static void sigint_handler(int sig) {
    (void)sig;
    running = 0;
}

static uint8_t mcore_virtio_get_status(struct virtio_device *vdev) {
    (void)vdev;
    return *(volatile uint8_t *)((char *)VRING0_ADDR + 0x7ff0);
}

static void mcore_virtio_set_status(struct virtio_device *vdev, uint8_t status) {
    (void)vdev;
    *(volatile uint8_t *)((char *)VRING0_ADDR + 0x7ff0) = status;
}

static uint32_t mcore_virtio_get_features(struct virtio_device *vdev) {
    (void)vdev;
    return 0;
}

static void mcore_virtio_set_features(struct virtio_device *vdev, uint32_t features) {
    (void)vdev;
    (void)features;
}

/* OpenAMP VirtIO notification callback */
static void virtio_notify(struct virtqueue *vq) {
    (void)vq;
    // Write 0 to TRIG register at offset 0x2c000 to notify A-Core
    *(volatile uint32_t*)((char *)VRING0_ADDR + 0x2c000) = 0;
}

static const struct virtio_dispatch mcore_virtio_dispatch = {
    .get_status = mcore_virtio_get_status,
    .set_status = mcore_virtio_set_status,
    .get_features = mcore_virtio_get_features,
    .set_features = mcore_virtio_set_features,
    .notify = virtio_notify,
};

static int demo_ept_cb(struct rpmsg_endpoint *ept, void *data, size_t len, uint32_t src, void *priv) {
    (void)priv;
    (void)src;
    
    printf("[M-Core] Received message: '%.*s'\n", (int)len, (char *)data);
    
    char reply[512];
    snprintf(reply, sizeof(reply), "[Baremetal Echo] %.*s", (int)len, (char *)data);
    
    /* Echo message back to A-Core */
    printf("[M-Core] Sending reply: '%s'\n", reply);
    struct virtqueue *svq = rvdev.svq;
    if (svq && svq->vq_ring.used) {
        printf("[M-Core] Before reply send: svq used_ptr=%p, used->idx=%u\n",
               (void*)svq->vq_ring.used, svq->vq_ring.used->idx);
    }
    int send_ret = rpmsg_send(ept, reply, strlen(reply));
    if (send_ret < 0) {
        printf("[M-Core] ERROR: rpmsg_send failed: %d\n", send_ret);
    } else {
        printf("[M-Core] Sent reply successfully\n");
    }
    if (svq && svq->vq_ring.used) {
        printf("[M-Core] After reply send: svq used->idx=%u\n", svq->vq_ring.used->idx);
    }
    return RPMSG_SUCCESS;
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);

    signal(SIGUSR1, sigusr1_handler);
    signal(SIGINT, sigint_handler);
    signal(SIGTERM, sigint_handler);

    /* Initialize libmetal */
    struct metal_init_params init_params = METAL_INIT_DEFAULTS;
    if (metal_init(&init_params) != 0) {
        fprintf(stderr, "Failed to initialize libmetal\n");
        return 1;
    }

    /* Set up metal device/IO region mapped at fixed virtual address 0x3ee00000 */
    struct metal_device *mdev = NULL;
    struct metal_io_region *io = NULL;
    static metal_phys_addr_t shm_phys[] = { VRING0_ADDR };
    static struct metal_device shm_dev = {
        .name = "shm",
        .num_regions = 1,
        .regions = {
            {
                .virt = (void*)VRING0_ADDR,
                .physmap = shm_phys,
                .size = SHM_SIZE,
                .page_shift = 18,
                .page_mask = -1,
                .mem_flags = 0,
            }
        }
    };
    int ret = metal_register_generic_device(&shm_dev);
    if (ret != 0) {
        fprintf(stderr, "Failed to register metal device\n");
        metal_finish();
        return 1;
    }
    ret = metal_device_open("generic", "shm", &mdev);
    if (ret != 0) {
        fprintf(stderr, "Failed to open metal device\n");
        metal_finish();
        return 1;
    }
    io = metal_device_io_region(mdev, 0);
    if (!io) {
        fprintf(stderr, "Failed to get metal IO region\n");
        metal_device_close(mdev);
        metal_finish();
        return 1;
    }

    /* Initialize VirtIO Device as REMOTE */
    static struct virtio_device vdev;
    vdev.role = VIRTIO_DEV_DEVICE;
    vdev.vrings_num = 2;
    vdev.func = &mcore_virtio_dispatch;
    
    static struct virtio_vring_info vrings[2];
    vrings[0].io = io;
    vrings[0].info.align = VRING_ALIGN;
    vrings[0].info.num_descs = VRING_SIZE;
    vrings[0].info.vaddr = (void*)VRING0_ADDR;
    vrings[0].vq = virtqueue_allocate(0);
    vrings[0].notifyid = 100;
    vrings[1].io = io;
    vrings[1].info.align = VRING_ALIGN;
    vrings[1].info.num_descs = VRING_SIZE;
    vrings[1].info.vaddr = (void*)VRING1_ADDR;
    vrings[1].vq = virtqueue_allocate(0);
    vrings[1].notifyid = 101;

    vdev.vrings_info = vrings;
    
    /* Initialize RPMsg VirtIO device and its shared memory pool */
    static struct rpmsg_virtio_shm_pool shpool;
    rpmsg_virtio_init_shm_pool(&shpool, (void*)BUF_ADDR, 0x20000); /* Limit to uio2 (vringbuf) size */
    
    ret = rpmsg_init_vdev(&rvdev, &vdev, NULL, io, &shpool);
    if (ret != 0) {
        fprintf(stderr, "Failed to initialize rpmsg_virtio: %d\n", ret);
        metal_device_close(mdev);
        metal_finish();
        return 1;
    }
    struct rpmsg_device *rdev = rpmsg_virtio_get_rpmsg_device(&rvdev);

    /* Create RPMsg endpoint */
    ret = rpmsg_create_ept(&ept, rdev, "rpmsg-openamp-demo-channel", 101, 100, demo_ept_cb, NULL);
    if (ret != 0) {
        fprintf(stderr, "Failed to create rpmsg endpoint: %d\n", ret);
        rpmsg_deinit_vdev(&rvdev);
        metal_device_close(mdev);
        metal_finish();
        return 1;
    }

    printf("[M-Core Baremetal] Ready, waiting for RPMsg events...\n");
    fflush(stdout);

    /* Main Dispatch Loop */
    while (running) {
        if (sigusr1_fired) {
            sigusr1_fired = 0;
            printf("[M-Core] Processing VRING0 notification...\n");
            struct virtqueue *vq = rvdev.rvq;
            if (vq && vq->vq_ring.used) {
                printf("[M-Core] Before notify: cons_idx=%u, used->idx=%u, used_ptr=%p\n",
                       vq->vq_used_cons_idx, vq->vq_ring.used->idx, (void*)vq->vq_ring.used);
            }
            /* Received simulated IPI interrupt from A-Core, run VirtIO dispatch functions */
            rproc_virtio_notified(rvdev.vdev, 101);  /* Trigger vring 1 (A->M) */
            if (vq && vq->vq_ring.used) {
                printf("[M-Core] After notify: cons_idx=%u, used->idx=%u\n",
                       vq->vq_used_cons_idx, vq->vq_ring.used->idx);
            }
        }
        
        /* Low overhead sleep/yield */
        usleep(500);
    }

    /* Cleanup */
    rpmsg_destroy_ept(&ept);
    rpmsg_deinit_vdev(&rvdev);
    virtqueue_free(vrings[0].vq);
    virtqueue_free(vrings[1].vq);
    metal_device_close(mdev);
    metal_finish();
    
    printf("[M-Core Baremetal] Terminated successfully.\n");
    return 0;
}
