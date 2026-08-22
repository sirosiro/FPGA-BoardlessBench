# bash completion for fbb (FPGA-BoardlessBench CLI)

_fbb_completions() {
    local cur prev subcmds
    COMPREPLY=()
    cur="${COMP_WORDS[COMP_CWORD]}"
    prev="${COMP_WORDS[COMP_CWORD-1]}"
    subcmds="new test inspect plugin"

    if [ $COMP_CWORD -eq 1 ]; then
        COMPREPLY=( $(compgen -W "${subcmds}" -- ${cur}) )
        return 0
    fi

    case "${COMP_WORDS[1]}" in
        test|inspect)
            local scenarios_dir="/workspaces/FPGA-BoardlessBench/tests/scenarios"
            local scenarios=""
            if [ -d "$scenarios_dir" ]; then
                scenarios=$(ls -d ${scenarios_dir}/*/ 2>/dev/null | xargs -n1 basename 2>/dev/null)
            fi
            COMPREPLY=( $(compgen -W "${scenarios}" -- ${cur}) $(compgen -f -- ${cur}) )
            return 0
            ;;
        plugin)
            if [ $COMP_CWORD -eq 2 ]; then
                COMPREPLY=( $(compgen -W "list install remove" -- ${cur}) )
                return 0
            fi
            if [ "${COMP_WORDS[2]}" = "remove" ]; then
                local user_plugins_dir="$HOME/.fbb/plugins"
                local plugins=""
                if [ -d "$user_plugins_dir" ]; then
                    plugins=$(ls "$user_plugins_dir" 2>/dev/null)
                fi
                COMPREPLY=( $(compgen -W "${plugins}" -- ${cur}) )
                return 0
            fi
            ;;
        new)
            if [ "$prev" = "--target" ]; then
                COMPREPLY=( $(compgen -W "zynq7000 imx8mp" -- ${cur}) )
                return 0
            fi
            ;;
    esac
}

complete -F _fbb_completions fbb
