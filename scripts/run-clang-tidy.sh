#!/usr/bin/env bash
# Wrapper script to run clang-tidy with Nix system includes and resource directory injected

EXTRA_ARGS=("-extra-arg=-stdlib=libc++")

# Dynamically get compiler's resource directory for built-in headers like stdarg.h
if command -v clang++ >/dev/null 2>&1; then
    RESOURCE_DIR=$(clang++ -print-resource-dir)
    if [ -n "$RESOURCE_DIR" ]; then
        EXTRA_ARGS+=("-extra-arg=-resource-dir=${RESOURCE_DIR}")
    fi
fi

if [ -n "$NIX_CFLAGS_COMPILE" ]; then
    # Split the NIX_CFLAGS_COMPILE environment variable by spaces
    read -r -a flags <<< "$NIX_CFLAGS_COMPILE"
    i=0
    while [ $i -lt ${#flags[@]} ]; do
        flag="${flags[$i]}"
        if [ "$flag" = "-isystem" ]; then
            next_idx=$((i + 1))
            if [ $next_idx -lt ${#flags[@]} ]; then
                next_flag="${flags[$next_idx]}"
                # Add the original system include path
                EXTRA_ARGS+=("-extra-arg=-isystem${next_flag}")
                
                # If this is libcxx and has c++/v1 subdirectory, add it as well
                if [ -d "${next_flag}/c++/v1" ]; then
                    EXTRA_ARGS+=("-extra-arg=-isystem${next_flag}/c++/v1")
                fi
                
                i=$next_idx
            else
                EXTRA_ARGS+=("-extra-arg=$flag")
            fi
        else
            EXTRA_ARGS+=("-extra-arg=$flag")
        fi
        i=$((i + 1))
    done
fi

exec clang-tidy "${EXTRA_ARGS[@]}" "$@"
