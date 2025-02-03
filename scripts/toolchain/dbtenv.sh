#!/usr/bin/env bash

# shellcheck disable=SC2034,SC2016,SC2086

# public variables
DEFAULT_SCRIPT_PATH="$(pwd -P)";
DBT_TOOLCHAIN_REQUIRED_VERSION="$(cat ${DEFAULT_SCRIPT_PATH}/toolchain/REQUIRED_VERSION)"
DBT_TOOLCHAIN_VERSION="${DBT_TOOLCHAIN_VERSION:-"${DBT_TOOLCHAIN_REQUIRED_VERSION}"}";

if [ -z ${DBT_TOOLCHAIN_PATH+x} ] ; then
    DBT_TOOLCHAIN_PATH_WAS_SET=0;
else
    DBT_TOOLCHAIN_PATH_WAS_SET=1;
fi

DBT_TOOLCHAIN_PATH="${DBT_TOOLCHAIN_PATH:-$DEFAULT_SCRIPT_PATH}";
DBT_VERBOSE="${DBT_VERBOSE:-""}";
DBT_DID_UNPACKING=""

dbtenv_show_usage()
{
    echo "Running this script manually is wrong, please source it";
    echo "Example:";
    printf "\tsource scripts/toolchain/dbtenv.sh\n";
    echo "To restore your environment, source dbtenv.sh with '--restore'."
    echo "Example:";
    printf "\tsource scripts/toolchain/dbtenv.sh --restore\n";
}

dbtenv_curl()
{
    curl --progress-bar -SLo "$1" "$2";
}

dbtenv_wget()
{
    wget --show-progress --progress=bar:force -qO "$1" "$2";
}

dbtenv_restore_env()
{
    TOOLCHAIN_ARCH_DIR_SED="$(echo "$TOOLCHAIN_ARCH_DIR" | sed 's/\//\\\//g')"
    PATH="$(echo "$PATH" | /usr/bin/sed "s/$TOOLCHAIN_ARCH_DIR_SED\/python\/bin://g")";
    PATH="$(echo "$PATH" | /usr/bin/sed "s/$TOOLCHAIN_ARCH_DIR_SED\/arm-none-eabi-gcc\/bin://g")";
    PATH="$(echo "$PATH" | /usr/bin/sed "s/$TOOLCHAIN_ARCH_DIR_SED\/openocd\/bin://g")";
    PATH="$(echo "$PATH" | /usr/bin/sed "s/$TOOLCHAIN_ARCH_DIR_SED\/clang\/bin://g")";
    PATH="$(echo "$PATH" | /usr/bin/sed "s/$TOOLCHAIN_ARCH_DIR_SED\/cmake\/bin://g")";
    PATH="$(echo "$PATH" | /usr/bin/sed "s/$TOOLCHAIN_ARCH_DIR_SED\/ninja-build\/bin://g")";
    # PATH="$(echo "$PATH" | /usr/bin/sed "s/$TOOLCHAIN_ARCH_DIR_SED\/openssl\/bin://g")";
    if [ -n "${PS1:-""}" ]; then
        PS1="$(echo "$PS1" | sed 's/\[dbt\] //g')";
    elif [ -n "${PROMPT:-""}" ]; then
        PROMPT="$(echo "$PROMPT" | sed 's/\[dbt\] //g')";
    fi

    if [ -n "$SAVED_SSL_CERT_FILE" ]; then
        export SSL_CERT_FILE="$SAVED_SSL_CERT_FILE";
        export REQUESTS_CA_BUNDLE="$SAVED_REQUESTS_CA_BUNDLE";
    else
        unset SSL_CERT_FILE;
        unset REQUESTS_CA_BUNDLE;
    fi

    if [ "$SYS_TYPE" = "Linux" ]; then
        if [ -n "$SAVED_TERMINFO_DIRS" ]; then
            export TERMINFO_DIRS="$SAVED_TERMINFO_DIRS";
        else
            unset TERMINFO_DIRS;
        fi
        unset SAVED_TERMINFO_DIRS;
    fi

    export PYTHONNOUSERSITE="$SAVED_PYTHONNOUSERSITE";
    export PYTHONPATH="$SAVED_PYTHONPATH";
    export PYTHONHOME="$SAVED_PYTHONHOME";

    unset SAVED_SSL_CERT_FILE;
    unset SAVED_REQUESTS_CA_BUNDLE;
    unset SAVED_PYTHONNOUSERSITE;
    unset SAVED_PYTHONPATH;
    unset SAVED_PYTHONHOME;

    unset DBT_TOOLCHAIN_VERSION;
    unset DBT_TOOLCHAIN_PATH;
}

dbtenv_check_sourced()
{
    case "${ZSH_EVAL_CONTEXT:-""}" in *:file:*)
        setopt +o nomatch;  # disabling 'no match found' warning in zsh
        return 0;;
    esac
    if [ ${0##*/} = "dbtenv.sh" ]; then  # exluding script itself
        dbtenv_show_usage;
        return 1;
    fi
    case ${0##*/} in dash|-dash|bash|-bash|ksh|-ksh|sh|-sh|*.sh|dbt|udbt)
        return 0;;
    esac
    dbtenv_show_usage;
    return 1;
}

dbtenv_check_if_sourced_multiple_times()
{
    if ! echo "${PS1:-""}" | grep -qF "[dbt] "; then
        if ! echo "${PROMPT:-""}" | grep -qF "[dbt] "; then
            return 0;
        fi
    fi
    echo "Warning! DBT environment script was sourced more than once!";
    echo "You might be doing things wrong, please open a new shell!";
    return 1;
}

dbtenv_set_shell_prompt()
{
    if [ -n "${PS1:-""}" ]; then
        PS1="[dbt] $PS1";
    elif [ -n "${PROMPT:-""}" ]; then
        PROMPT="[dbt] $PROMPT";
    fi
    return 0;  # all other shells
}

dbtenv_check_env_vars()
{
    # Return error if DBT_TOOLCHAIN_PATH is not set before script is sourced or if dbt executable is not in DEFAULT_SCRIPT_PATH
    if [ "$DBT_TOOLCHAIN_PATH_WAS_SET" -eq 0 ] && [ ! -x "$DEFAULT_SCRIPT_PATH/dbt" ] && [ ! -x "$DEFAULT_SCRIPT_PATH/udbt" ] ; then
        echo "Please source this script from [u]dbt root directory, or specify 'DBT_TOOLCHAIN_PATH' variable manually";
        echo "Example:";
        printf "\tDBT_TOOLCHAIN_PATH=DelugeFirmware source DelugeFirmware/scripts/dbtenv.sh\n";
        echo "If current directory is right, type 'unset DBT_TOOLCHAIN_PATH' and try again"
        return 1;
    fi
    return 0;
}

dbtenv_get_kernel_type()
{
    SYS_TYPE="$(uname -s | tr '[:upper:]' '[:lower:]')";
    ARCH_TYPE="$(uname -m | tr '[:upper:]' '[:lower:]')";
    # if [ "$ARCH_TYPE" != "x86_64" ] && [ "$SYS_TYPE" != "darwin" ]; then
    #     echo "We only provide toolchain for x86_64 CPUs, sorry..";
    #     return 1;
    # fi

    # Fudge the architecture type if we happen to be on a platform that uses
    # "aarch64" naming.
    if [[ "${ARCH_TYPE}" == "aarch64" ]]; then
        ARCH_TYPE="arm64"
    fi

    if [[ "${SYS_TYPE}" == "darwin" || "${SYS_TYPE}" == "linux" ]]; then
        # Disabling rosetta checking now that we've got native arm64
        # dbtenv_check_rosetta || return 1;
        TOOLCHAIN_ARCH_DIR="${DBT_TOOLCHAIN_PATH}/toolchain/v${DBT_TOOLCHAIN_VERSION}/${SYS_TYPE}-${ARCH_TYPE}";
        TOOLCHAIN_URL="https://github.com/SynthstromAudible/dbt-toolchain/releases/download/v${DBT_TOOLCHAIN_VERSION}/dbt-toolchain-${DBT_TOOLCHAIN_VERSION}-${SYS_TYPE}-${ARCH_TYPE}.tar.gz";
    elif echo "$SYS_TYPE" | grep -q "MINGW"; then
        echo "In MinGW shell, use \"[u]dbt.cmd\" instead of \"[u]dbt\"";
        return 1;
    else
        echo "Your system configuration is not supported. Sorry.. Please report us your configuration.";
        return 1;
    fi
    return 0;
}

dbtenv_check_rosetta()
{
    if [ "$ARCH_TYPE" = "arm64" ]; then
        if ! /usr/bin/pgrep -q oahd; then
            echo "Deluge Build Toolchain needs Rosetta2 to run under Apple Silicon";
            echo "Please install it by typing 'softwareupdate --install-rosetta --agree-to-license'";
            return 1;
        fi
    fi
    return 0;
}

dbtenv_check_tar()
{
    printf "Checking for tar..";
    if ! tar --version > /dev/null 2>&1; then
        echo "no";
        return 1;
    fi
    echo "yes";
    return 0;
}

dbtenv_check_downloaded_toolchain()
{
    printf "Checking if downloaded toolchain tgz exists..";
    if [ ! -f "$DBT_TOOLCHAIN_PATH/toolchain/$TOOLCHAIN_TAR" ]; then
        echo "no";
        return 1;
    fi
    echo "yes";
    return 0;
}

dbtenv_download_toolchain_tar()
{
    echo "Downloading toolchain:";
    mkdir -p "$DBT_TOOLCHAIN_PATH/toolchain" || return 1;
    "$DBT_DOWNLOADER" "$DBT_TOOLCHAIN_PATH/toolchain/$TOOLCHAIN_TAR.part" "$TOOLCHAIN_URL" || return 1;
    # restoring original filename if file downloaded successfully
    mv "$DBT_TOOLCHAIN_PATH/toolchain/$TOOLCHAIN_TAR.part" "$DBT_TOOLCHAIN_PATH/toolchain/$TOOLCHAIN_TAR"
    echo "done";
    return 0;
}

dbtenv_show_unpack_percentage()
{
    LINE=0;
    while read -r line; do
        LINE=$(( LINE + 1 ));
        if [ $(( LINE % 300 )) -eq 0 ]; then
            printf "#";
        fi
    done
    echo " 100.0%";
}

dbtenv_unpack_toolchain()
{
    echo "Unpacking toolchain to '$DBT_TOOLCHAIN_PATH/toolchain/v${DBT_TOOLCHAIN_VERSION}':";
    mkdir -p "$DBT_TOOLCHAIN_PATH/toolchain/v${DBT_TOOLCHAIN_VERSION}" || return 1;
    tar -xvf "$DBT_TOOLCHAIN_PATH/toolchain/$TOOLCHAIN_TAR" -C "$DBT_TOOLCHAIN_PATH/toolchain/v${DBT_TOOLCHAIN_VERSION}/" 2>&1 | dbtenv_show_unpack_percentage;
    mkdir -p "$DBT_TOOLCHAIN_PATH/toolchain" || return 1;
    mv "$DBT_TOOLCHAIN_PATH/toolchain/$TOOLCHAIN_DIR" "$TOOLCHAIN_ARCH_DIR" || return 1;
    echo "done";

    DBT_DID_UNPACKING=1

    return 0;
}

dbtenv_setup_python()
{
    if [ -z "$DBT_NO_PYTHON_UPGRADE" ]; then
        # Install python wheels if not already installed, upgrade pip
        # afterward (needs certifi in place).
        pip_cmd="python3 -m pip";
        pip_requirements_path="${SCRIPT_PATH}/scripts/toolchain/requirements.txt"
        pip_wheel_path="${TOOLCHAIN_ARCH_DIR}/python/wheel";
        pip_certifi_wheel=$(find $pip_wheel_path/certifi*)
        $pip_cmd install -q "${pip_certifi_wheel}"
        $pip_cmd install -q --upgrade pip
        $pip_cmd install -q -f "${pip_wheel_path}" -r "${pip_requirements_path}"
    fi
}

dbtenv_cleanup()
{
    
    # Kludgey but, quick path to getting rid of these
    printf "Removing any nuisance mac dotfiles..";
    find toolchain | grep -e '/[.][^\/]*$' | sed -e 's/\(.*\)/rm \"\1\"/' | /usr/bin/env bash;
    echo "done";

    printf "Cleaning up..";
    
    if [ -n "${DBT_TOOLCHAIN_PATH:-""}" ]; then
        rm -rf "${DBT_TOOLCHAIN_PATH:?}/toolchain/"*.tar.gz;
        rm -rf "${DBT_TOOLCHAIN_PATH:?}/toolchain/"*.part;
    fi
    echo "done";
    trap - 2;
    return 0;
}

dbtenv_curl_wget_check()
{
    printf "Checking curl..";
    if ! curl --version > /dev/null 2>&1; then
        echo "no";
        printf "Checking wget..";
        if ! wget --version > /dev/null 2>&1; then
            echo "no";
            echo "No curl or wget found in your PATH";
            echo "Please provide it or download this file:";
            echo;
            echo "$TOOLCHAIN_URL";
            echo;
            echo "And place in $DBT_TOOLCHAIN_PATH/toolchain/ dir manually";
            return 1;
        fi
        echo "yes"
        DBT_DOWNLOADER="dbtenv_wget";
        return 0;
    fi
    echo "yes"
    DBT_DOWNLOADER="dbtenv_curl";
    return 0;
}

dbtenv_check_download_toolchain()
{
    if [ ! -d "$TOOLCHAIN_ARCH_DIR" ]; then
        dbtenv_download_toolchain || return 1;
    elif [ ! -f "$TOOLCHAIN_ARCH_DIR/VERSION" ]; then
        dbtenv_download_toolchain || return 1;
    elif [ "$(cat "$TOOLCHAIN_ARCH_DIR/VERSION")" -lt "$DBT_TOOLCHAIN_VERSION" ]; then
        echo "DBT: starting toolchain upgrade process.."
        dbtenv_download_toolchain || return 1;
    fi
    return 0;
}

dbtenv_download_toolchain()
{
    dbtenv_check_tar || return 1;
    TOOLCHAIN_TAR="$(basename "$TOOLCHAIN_URL")";
    TOOLCHAIN_DIR="$(echo "$TOOLCHAIN_TAR" | sed "s/-$DBT_TOOLCHAIN_VERSION.tar.gz//g")";
    trap dbtenv_cleanup 2;  # trap will be restored in dbtenv_cleanup
    if ! dbtenv_check_downloaded_toolchain; then
        dbtenv_curl_wget_check || return 1;
        dbtenv_download_toolchain_tar || return 1;
    fi
    dbtenv_unpack_toolchain || return 1;
    dbtenv_cleanup;
    return 0;
}

dbtenv_print_version()
{
    if [ -n "$DBT_VERBOSE" ]; then
        echo "DBT: using toolchain version $(cat "$TOOLCHAIN_ARCH_DIR/VERSION")";
    fi
}

dbtenv_main()
{
    dbtenv_check_sourced || return 1;
    dbtenv_get_kernel_type || return 1;
    if [ "$1" = "--restore" ]; then
        dbtenv_restore_env;
        return 0;
    fi
    dbtenv_check_if_sourced_multiple_times;
    dbtenv_check_env_vars || return 1;
    dbtenv_check_download_toolchain || return 1;
    dbtenv_set_shell_prompt;
    dbtenv_print_version;
    PATH="$TOOLCHAIN_ARCH_DIR/python/bin:$PATH";
    PATH="$TOOLCHAIN_ARCH_DIR/arm-none-eabi-gcc/bin:$PATH";
    PATH="$TOOLCHAIN_ARCH_DIR/openocd/bin:$PATH";
    PATH="$TOOLCHAIN_ARCH_DIR/clang/bin:$PATH";
    PATH="$TOOLCHAIN_ARCH_DIR/cmake/bin:$PATH";
    PATH="$TOOLCHAIN_ARCH_DIR/ninja-build/bin:$PATH";
    # PATH="$TOOLCHAIN_ARCH_DIR/openssl/bin:$PATH";
    export PATH;

    export SAVED_SSL_CERT_FILE="${SSL_CERT_FILE:-""}";
    export SAVED_REQUESTS_CA_BUNDLE="${REQUESTS_CA_BUNDLE:-""}";
    export SAVED_PYTHONNOUSERSITE="${PYTHONNOUSERSITE:-""}";
    export SAVED_PYTHONPATH="${PYTHONPATH:-""}";
    export SAVED_PYTHONHOME="${PYTHONHOME:-""}";

    export PYTHONNOUSERSITE=1;
    export PYTHONPATH="$DEFAULT_SCRIPT_PATH/scripts";
    export PYTHONHOME=;

    if [ "$SYS_TYPE" = "Linux" ]; then
        export SAVED_TERMINFO_DIRS="${TERMINFO_DIRS:-""}";
        export TERMINFO_DIRS="$TOOLCHAIN_ARCH_DIR/ncurses/share/terminfo";
    fi

    export SSL_CERT_FILE=$(python3 -c 'import site; print(site.getsitepackages()[-1])')/certifi/cacert.pem
    export REQUESTS_CA_BUNDLE="$SSL_CERT_FILE";

    if [ -n "${DBT_DID_UNPACKING}" ]; then
      dbtenv_setup_python
    fi
}

dbtenv_main "${1:-""}";
