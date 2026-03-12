#!/bin/bash
# ============================================================
# Flash / Deploy Script for Embedded Vision System
# Deploys built binary and configs to target device
# ============================================================

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m'

log_info()  { echo -e "${GREEN}[INFO]${NC}  $*"; }
log_warn()  { echo -e "${YELLOW}[WARN]${NC}  $*"; }
log_error() { echo -e "${RED}[ERROR]${NC} $*"; }

# Defaults
TARGET_HOST=""
TARGET_USER="root"
TARGET_DIR="/opt/vision"
TARGET_PORT=22
BINARY="build/vision_system"
METHOD="ssh"      # ssh, serial, sdcard, tftp
SERIAL_PORT="/dev/ttyUSB0"
SERIAL_BAUD=115200
SD_MOUNT="/mnt/sdcard"
RESTART_SERVICE=1
BACKUP_CONFIG=1

usage() {
    echo "Usage: $0 [OPTIONS]"
    echo ""
    echo "Options:"
    echo "  -H, --host HOST       Target hostname/IP (required for SSH/TFTP)"
    echo "  -u, --user USER       SSH user (default: root)"
    echo "  -d, --dir DIR         Target directory (default: /opt/vision)"
    echo "  -p, --port PORT       SSH port (default: 22)"
    echo "  -b, --binary FILE     Binary to flash (default: build/vision_system)"
    echo "  -m, --method METHOD   Deploy method: ssh|serial|sdcard|tftp"
    echo "  -s, --serial PORT     Serial port for serial flash"
    echo "  --no-restart          Don't restart service after deploy"
    echo "  --no-backup           Don't backup existing config"
    echo "  -h, --help            Show help"
    echo ""
    echo "Methods:"
    echo "  ssh     - Deploy via SSH/SCP (default)"
    echo "  serial  - Flash via serial (XMODEM)"
    echo "  sdcard  - Copy to mounted SD card"
    echo "  tftp    - Deploy via TFTP"
    echo ""
    echo "Examples:"
    echo "  $0 --host 192.168.1.100 --method ssh"
    echo "  $0 --method sdcard"
    echo "  $0 --host 192.168.1.100 --binary build_cmake/vision_system"
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        -H|--host)       TARGET_HOST="$2"; shift 2;;
        -u|--user)       TARGET_USER="$2"; shift 2;;
        -d|--dir)        TARGET_DIR="$2"; shift 2;;
        -p|--port)       TARGET_PORT="$2"; shift 2;;
        -b|--binary)     BINARY="$2"; shift 2;;
        -m|--method)     METHOD="$2"; shift 2;;
        -s|--serial)     SERIAL_PORT="$2"; shift 2;;
        --no-restart)    RESTART_SERVICE=0; shift;;
        --no-backup)     BACKUP_CONFIG=0; shift;;
        -h|--help)       usage; exit 0;;
        *)               log_error "Unknown: $1"; usage; exit 1;;
    esac
done

cd "$PROJECT_DIR"

# Verify binary exists
if [ ! -f "$BINARY" ]; then
    log_error "Binary not found: $BINARY"
    log_error "Run build.sh first"
    exit 1
fi

BINARY_SIZE=$(stat -f%z "$BINARY" 2>/dev/null || stat -c%s "$BINARY" 2>/dev/null)
log_info "Binary: $BINARY ($BINARY_SIZE bytes)"
log_info "Method: $METHOD"
log_info "Target: ${TARGET_USER}@${TARGET_HOST}:${TARGET_DIR}"

# Compute MD5 checksum
MD5=$(md5sum "$BINARY" 2>/dev/null | cut -d' ' -f1 || md5 -q "$BINARY" 2>/dev/null || echo "unknown")
log_info "MD5: $MD5"

case "$METHOD" in
    ssh)
        if [ -z "$TARGET_HOST" ]; then
            log_error "--host is required for SSH deploy"
            exit 1
        fi

        SSH_OPTS="-o StrictHostKeyChecking=no -o ConnectTimeout=10 -p $TARGET_PORT"
        SCP_OPTS="-P $TARGET_PORT -o StrictHostKeyChecking=no"
        REMOTE="${TARGET_USER}@${TARGET_HOST}"

        # Check connectivity
        log_info "Testing connection to $TARGET_HOST..."
        if ! ssh $SSH_OPTS "$REMOTE" "echo ok" >/dev/null 2>&1; then
            log_error "Cannot connect to $TARGET_HOST"
            exit 1
        fi
        log_info "Connection OK"

        # Stop existing service
        if [ $RESTART_SERVICE -eq 1 ]; then
            log_info "Stopping vision service on target..."
            ssh $SSH_OPTS "$REMOTE" \
                "killall vision_system 2>/dev/null; sleep 1" || true
        fi

        # Backup config
        if [ $BACKUP_CONFIG -eq 1 ]; then
            log_info "Backing up config..."
            ssh $SSH_OPTS "$REMOTE" \
                "[ -f ${TARGET_DIR}/etc/config.yaml ] && cp ${TARGET_DIR}/etc/config.yaml ${TARGET_DIR}/etc/config.yaml.bak" \
                2>/dev/null || true
        fi

        # Create directories
        log_info "Creating directories..."
        ssh $SSH_OPTS "$REMOTE" \
            "mkdir -p ${TARGET_DIR}/bin ${TARGET_DIR}/etc ${TARGET_DIR}/models ${TARGET_DIR}/log"

        # Transfer binary
        log_info "Transferring binary..."
        scp $SCP_OPTS "$BINARY" "${REMOTE}:${TARGET_DIR}/bin/vision_system"

        # Transfer config
        log_info "Transferring config..."
        scp $SCP_OPTS "config.yaml" "${REMOTE}:${TARGET_DIR}/etc/"

        # Set permissions
        ssh $SSH_OPTS "$REMOTE" "chmod +x ${TARGET_DIR}/bin/vision_system"

        # Verify checksum
        REMOTE_MD5=$(ssh $SSH_OPTS "$REMOTE" \
            "md5sum ${TARGET_DIR}/bin/vision_system 2>/dev/null | cut -d' ' -f1" || echo "")
        if [ "$MD5" = "$REMOTE_MD5" ]; then
            log_info "Checksum verified: OK"
        else
            log_warn "Checksum mismatch! local=$MD5 remote=$REMOTE_MD5"
        fi

        # Restart service
        if [ $RESTART_SERVICE -eq 1 ]; then
            log_info "Starting vision service..."
            ssh $SSH_OPTS "$REMOTE" \
                "cd ${TARGET_DIR} && nohup bin/vision_system -c etc/config.yaml -d > /dev/null 2>&1 &"
            sleep 2

            # Check if running
            RUNNING=$(ssh $SSH_OPTS "$REMOTE" "pgrep vision_system" 2>/dev/null || echo "")
            if [ -n "$RUNNING" ]; then
                log_info "Service started successfully (PID: $RUNNING)"
            else
                log_warn "Service may not have started"
            fi
        fi
        ;;

    sdcard)
        # Find SD card mount
        if [ -d "$SD_MOUNT" ]; then
            MOUNT="$SD_MOUNT"
        elif [ -d "/media/$USER" ]; then
            MOUNT=$(ls -d /media/$USER/* 2>/dev/null | head -1)
        elif [ -d "/run/media/$USER" ]; then
            MOUNT=$(ls -d /run/media/$USER/* 2>/dev/null | head -1)
        else
            log_error "No SD card mount found. Specify with SD_MOUNT variable"
            exit 1
        fi

        log_info "Using SD card mount: $MOUNT"

        TARGET="${MOUNT}${TARGET_DIR}"
        mkdir -p "${TARGET}/bin" "${TARGET}/etc" "${TARGET}/models" "${TARGET}/log"

        log_info "Copying binary..."
        cp "$BINARY" "${TARGET}/bin/vision_system"
        chmod +x "${TARGET}/bin/vision_system"

        log_info "Copying config..."
        cp config.yaml "${TARGET}/etc/"

        # Create startup script
        cat > "${MOUNT}/etc/init.d/S99vision" << 'INITEOF'
#!/bin/sh
### BEGIN INIT INFO
# Provides:          vision_system
# Required-Start:    $local_fs
# Default-Start:     2 3 4 5
# Default-Stop:      0 1 6
# Description:       Embedded Vision System
### END INIT INFO

DAEMON=/opt/vision/bin/vision_system
DAEMON_ARGS="-c /opt/vision/etc/config.yaml -d"
PIDFILE=/var/run/vision_system.pid

case "$1" in
    start)
        echo "Starting vision system..."
        start-stop-daemon --start --background --make-pidfile \
            --pidfile "$PIDFILE" --exec "$DAEMON" -- $DAEMON_ARGS
        ;;
    stop)
        echo "Stopping vision system..."
        start-stop-daemon --stop --pidfile "$PIDFILE"
        rm -f "$PIDFILE"
        ;;
    restart)
        $0 stop
        sleep 1
        $0 start
        ;;
    status)
        if [ -f "$PIDFILE" ] && kill -0 $(cat "$PIDFILE") 2>/dev/null; then
            echo "Vision system is running (PID: $(cat $PIDFILE))"
        else
            echo "Vision system is not running"
        fi
        ;;
    *)
        echo "Usage: $0 {start|stop|restart|status}"
        exit 1
        ;;
esac
exit 0
INITEOF
        chmod +x "${MOUNT}/etc/init.d/S99vision" 2>/dev/null || true

        # Sync
        sync
        log_info "SD card flash complete"
        ;;

    serial)
        log_info "Serial flash via $SERIAL_PORT @ $SERIAL_BAUD"
        log_warn "Serial flash requires target bootloader support"

        if ! command -v sx >/dev/null 2>&1; then
            log_error "'sx' (XMODEM sender) not found. Install: apt-get install lrzsz"
            exit 1
        fi

        # Configure serial port
        stty -F "$SERIAL_PORT" "$SERIAL_BAUD" raw -echo -echoe -echok

        log_info "Sending binary via XMODEM..."
        sx "$BINARY" < "$SERIAL_PORT" > "$SERIAL_PORT"

        log_info "Serial transfer complete"
        log_warn "You may need to manually boot the target"
        ;;

    tftp)
        if [ -z "$TARGET_HOST" ]; then
            log_error "--host required for TFTP"
            exit 1
        fi

        TFTP_DIR="/srv/tftp"
        if [ ! -d "$TFTP_DIR" ]; then
            log_warn "TFTP directory not found, using /tmp"
            TFTP_DIR="/tmp/tftp"
            mkdir -p "$TFTP_DIR"
        fi

        log_info "Copying to TFTP directory..."
        cp "$BINARY" "${TFTP_DIR}/vision_system"
        cp config.yaml "${TFTP_DIR}/config.yaml"

        log_info "Files ready for TFTP download at ${TFTP_DIR}/"
        log_info "On target, run:"
        log_info "  tftp -g -r vision_system ${TARGET_HOST}"
        ;;

    *)
        log_error "Unknown method: $METHOD"
        usage
        exit 1
        ;;
esac

log_info "=========================================="
log_info "  Deploy Complete"
log_info "  Method:   $METHOD"
log_info "  Binary:   $BINARY ($BINARY_SIZE bytes)"
log_info "  Checksum: $MD5"
log_info "=========================================="