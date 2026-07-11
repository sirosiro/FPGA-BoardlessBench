import re
import os
from vfpga.models import Device, Register, I2CSlave, BoardModel

class DTSParser:
    @staticmethod
    def find_matching_braces(text, start_pos):
        brace_pos = text.find('{', start_pos)
        if brace_pos == -1:
            return -1, -1
        count = 1
        i = brace_pos + 1
        while i < len(text) and count > 0:
            if text[i] == '{':
                count += 1
            elif text[i] == '}':
                count -= 1
            i += 1
        if count == 0:
            return brace_pos, i
        return -1, -1

    @staticmethod
    def parse(dts_path):
        with open(dts_path, 'r') as f:
            content = f.read()
        devices = []
        
        # 1. Look for root node '/'
        root_match = re.search(r'/\s*\{', content)
        if root_match:
            brace_start, brace_end = DTSParser.find_matching_braces(content, root_match.start())
            if brace_start != -1:
                content = content[brace_start + 1 : brace_end - 1]
                
        pos = 0
        while True:
            # Match top-level nodes (e.g. node@1000 or label: node@1000)
            match = re.search(r'([a-zA-Z0-9_@:-]+)\s*\{', content[pos:])
            if not match:
                break
            match_start = pos + match.start()
            raw_name = match.group(1).strip()
            
            brace_start, brace_end = DTSParser.find_matching_braces(content, match_start)
            if brace_start == -1:
                pos = match_start + len(raw_name) + 1
                continue
            
            body = content[brace_start + 1 : brace_end - 1]
            pos = brace_end
            
            # Extract node name after label if colon exists
            node_name = raw_name
            if ':' in raw_name:
                node_name = raw_name.split(':')[-1].strip()
                
            name = node_name.split('@')[0]

            # Clean body by removing nested sub-node blocks to prevent property overriding
            clean_body = body
            while True:
                sub_match = re.search(r'([a-zA-Z0-9_@:-]+)\s*\{', clean_body)
                if not sub_match:
                    break
                sub_start = sub_match.start()
                sub_brace_start, sub_brace_end = DTSParser.find_matching_braces(clean_body, sub_start)
                if sub_brace_start == -1:
                    break
                clean_body = clean_body[:sub_start] + clean_body[sub_brace_end:]

            props = {}
            prop_matches = re.finditer(r'([a-zA-Z0-9_-]+)\s*=\s*([^;]+);', clean_body)
            for p_match in prop_matches:
                k = p_match.group(1).strip()
                v = p_match.group(2).strip()
                if '{' in v or '}' in v:
                    continue
                if v.startswith('<') and v.endswith('>'): v = v[1:-1].strip()
                if v.startswith('"') and v.endswith('"'): v = v[1:-1].strip()
                props[k] = v
                
            if 'compatible' in props:
                compatible = props.get('compatible', '')
                label = props.get('label', "/dev/%s" % name)
                dev_type = 'unknown'
                if 'generic-uio' in compatible: dev_type = 'uio'
                elif 'i2c' in compatible or 'cdns,i2c' in compatible: dev_type = 'i2c'
                elif 'uart' in compatible or 'xlnx,xps-uart' in compatible: dev_type = 'uart'
                elif 'gpio' in compatible or 'xlnx,xps-gpio' in compatible: dev_type = 'gpio'
                elif 'rpmsg' in compatible: dev_type = 'rpmsg'
                if dev_type == 'unknown' and label.startswith('/dev/uio'):
                    dev_type = 'uio'
                
                device = Device(name, label, dev_type, props.get('reg', '0x0 0x0'))
                for k, v in props.items():
                    if k not in ['label', 'compatible', 'reg', 'registers']: device.extra_props[k] = v
                
                # Parse nested I2C slave devices
                if dev_type == 'i2c':
                    sub_pos = 0
                    while True:
                        sub_match = re.search(r'([a-zA-Z0-9_@:-]+)\s*\{', body[sub_pos:])
                        if not sub_match:
                            break
                        sub_match_start = sub_pos + sub_match.start()
                        sub_raw_name = sub_match.group(1).strip()
                        
                        sub_brace_start, sub_brace_end = DTSParser.find_matching_braces(body, sub_match_start)
                        if sub_brace_start == -1:
                            sub_pos = sub_match_start + len(sub_raw_name) + 1
                            continue
                        
                        sub_body = body[sub_brace_start + 1 : sub_brace_end - 1]
                        sub_pos = sub_brace_end
                        
                        s_node_name = sub_raw_name
                        if ':' in sub_raw_name:
                            s_node_name = sub_raw_name.split(':')[-1].strip()
                            
                        s_name = s_node_name.split('@')[0]
                        s_addr_str = s_node_name.split('@')[1] if '@' in s_node_name else "0"
                        try:
                            s_addr = int(s_addr_str, 16)
                        except:
                            s_addr = 0
                            
                        s_props = {}
                        s_prop_matches = re.finditer(r'([a-zA-Z0-9_-]+)\s*=\s*([^;]+);', sub_body)
                        for sp_match in s_prop_matches:
                            sk = sp_match.group(1).strip()
                            sv = sp_match.group(2).strip()
                            if sv.startswith('<') and sv.endswith('>'): sv = sv[1:-1].strip()
                            if sv.startswith('"') and sv.endswith('"'): sv = sv[1:-1].strip()
                            s_props[sk] = sv
                        
                        if 'compatible' in s_props:
                            init_val_str = s_props.get('fbb,mock-data', '0x10')
                            try:
                                init_val = int(init_val_str, 0)
                            except:
                                init_val = 0x10
                            mock_file = s_props.get('fbb,mock-file', None)
                            slave = I2CSlave(s_name, s_addr, s_props['compatible'], mock_file, init_val)
                            device.i2c_slaves.append(slave)
                
                if 'registers' in props:
                    reg_raw = props['registers'].replace('\\n', ' ').replace('\\"', '').replace('\\t', ' ')
                    reg_list = reg_raw.split(',')
                    for r_str in reg_list:
                        r_str = r_str.strip().strip('"').strip()
                        if '@' in r_str:
                            reg_parts = r_str.split('@')
                            reg_name = reg_parts[0].strip()
                            reg_offset = reg_parts[1].strip()
                            
                            logical_name = None
                            paren_match = re.match(r'^([a-zA-Z0-9_]+)\s*\(\s*([a-zA-Z0-9_]+)\s*\)$', reg_name)
                            if paren_match:
                                reg_name = paren_match.group(1)
                                logical_name = paren_match.group(2)
                            else:
                                logical_name = reg_name
                            
                            device.registers.append(Register(reg_name, reg_offset, 'RW', logical_name))
                devices.append(device)
        
        # 共有メモリ名として使用するボード名を決定（UIO > GPIO > デフォルト）
        board_name = "vfpga_reg"
        uio = next((d for d in devices if d.type == 'uio'), None)
        if uio:
            board_name = uio.name
        else:
            gpio = next((d for d in devices if d.type == 'gpio'), None)
            if gpio: board_name = gpio.name
        
        # 最上位ノードの compatible を抽出してヌル文字区切りのバイト列にする
        compatible_bytes = b"generic,fbb-vfpga\x00"
        root_compat_match = re.search(r'/\s*\{[^{]*?compatible\s*=\s*([^;]+);', content, re.DOTALL)
        if root_compat_match:
            raw_compat = root_compat_match.group(1).strip()
            parts = [p.strip().strip('"').strip() for p in raw_compat.split(',')]
            compatible_bytes = b"".join([p.encode('utf-8') + b"\x00" for p in parts if p])

        # 最上位ノードの model を抽出
        model_name = "generic-vfpga"
        root_model_match = re.search(r'/\s*\{[^{]*?model\s*=\s*"([^"]+)";', content, re.DOTALL)
        if root_model_match:
            model_name = root_model_match.group(1).strip()

        model = BoardModel(devices, name=board_name)
        model.compatible_bytes = compatible_bytes
        model.model_name = model_name
        model.scenario_dir = os.path.dirname(dts_path)
        return model
