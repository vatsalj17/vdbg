# shellcheck disable=SC2207
# bash completion for vdbg -*- shell-script -*-

_vdbg() {
    local cur prev opts commands
    COMPREPLY=()
    cur="${COMP_WORDS[COMP_CWORD]}"
    prev="${COMP_WORDS[COMP_CWORD - 1]}"

    # check already present flags for mutual exclusivity
    local has_script=0
    local has_ex=0
    local i
    for ((i = 1; i < COMP_CWORD; i++)); do
        local w="${COMP_WORDS[i]}"
        if [[ "$w" == "--script" ]]; then
            has_script=1
        elif [[ "$w" == "-ex" || "$w" == "--execute" ]]; then
            has_ex=1
        fi
    done

    # filter available flags based on context
    opts=""
    if [[ $has_script -eq 0 && $has_ex -eq 0 ]]; then
        opts="-ex --execute --script -h --help"
    elif [[ $has_script -eq 1 ]]; then
        opts="-h --help"
    elif [[ $has_ex -eq 1 ]]; then
        opts="-ex --execute -h --help"
    fi

    # vdbg interactive commands (for -ex / --execute completion)
    commands="arguments break backtrace continue clear context delete disable disassemble \
              exit enable finish files functions help header list ls memory next plt \
              run restart register step stepi source sections symbols vmmap"

    # 1. complete argument for --script
    if [[ "$prev" == "--script" ]]; then
        COMPREPLY=($(compgen -f -- "$cur"))
        return 0
    fi

    # 2. complete argument for -ex / --execute
    if [[ "$prev" == "-ex" || "$prev" == "--execute" ]]; then
        # keep the quote open
        compopt -o noquote
        COMPREPLY=($(compgen -W "$commands" -- "$cur"))
        return 0
    fi

    # 3. if typing a flag, suggest flags
    if [[ "$cur" == -* ]]; then
        COMPREPLY=($(compgen -W "$opts" -- "$cur"))
        return 0
    fi

    # 4. otherwise, suggest files
    COMPREPLY=($(compgen -f -- "$cur"))
    return 0
}

complete -o filenames -F _vdbg vdbg ./vdbg
