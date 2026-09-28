#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

void execute_cmd_abort(const char *cmd, const char *err_msg) {
    if (system(cmd) != 0) {
        printf("\nFATAL ERROR: %s\nCommand failed: %s\nExiting installation.\n", err_msg, cmd);
        exit(1);
    }
}

void execute_cmd(const char *cmd) {
    system(cmd);
}

void get_string(const char *prompt, char *out, size_t size) {
    printf("%s", prompt);
    char format[16];
    snprintf(format, sizeof(format), " %%%zu[^\n]", size - 1);
    scanf(format, out);
}

int check_internet(void) {
    return system("ping -c 1 8.8.8.8 > /dev/null 2>&1") == 0;
}

void setup_wifi(void) {
    char ssid[128];
    char pass[128];
    char cmd[512];

    printf("Scanning for Wi-Fi networks...\n");
    execute_cmd("nmcli dev wifi rescan > /dev/null 2>&1");
    sleep(3);
    execute_cmd("nmcli dev wifi list");

    get_string("\nEnter SSID: ", ssid, sizeof(ssid));
    get_string("Enter Wi-Fi Password: ", pass, sizeof(pass));

    snprintf(cmd, sizeof(cmd), "nmcli dev wifi connect '%s' password '%s'", ssid, pass);
    execute_cmd(cmd);
}

void ensure_network(void) {
    if (check_internet()) {
        printf("Internet connection detected.\n");
        return;
    }

    if (system("ip link | grep -q -E 'wl|wlan'") == 0) {
        printf("No internet connection found, but Wi-Fi hardware is present.\n");
        char choice[8];
        get_string("Do you want to configure Wi-Fi? (y/n): ", choice, sizeof(choice));

        if (choice[0] == 'y' || choice[0] == 'Y') {
            setup_wifi();
            if (!check_internet()) {
                printf("Failed to connect to internet. Fuck off\n");
                exit(1);
            }
        } else {
            printf("Fuck off\n");
            exit(1);
        }
    } else {
        printf("Fuck off\n");
        exit(1);
    }
}

int check_uefi(void) {
    return access("/sys/firmware/efi", F_OK) == 0;
}

void select_disk(char *disk_out, size_t size) {
    printf("\nAvailable Disks:\n");
    execute_cmd("lsblk -d -n -o NAME,SIZE,MODEL | grep -v loop");

    char input[64];
    get_string("\nEnter disk name to install to (e.g. sda or nvme0n1): ", input, sizeof(input));

    if (strncmp(input, "/dev/", 5) == 0) {
        snprintf(disk_out, size, "%s", input);
    } else {
        snprintf(disk_out, size, "/dev/%s", input);
    }
}

void partition_disk(const char *disk, char *p1, char *p2, size_t p_size) {
    char cmd[512];

    if (strstr(disk, "nvme") != NULL || strstr(disk, "mmcblk") != NULL || strstr(disk, "loop") != NULL) {
        snprintf(p1, p_size, "%sp1", disk);
        snprintf(p2, p_size, "%sp2", disk);
    } else {
        snprintf(p1, p_size, "%s1", disk);
        snprintf(p2, p_size, "%s2", disk);
    }

    printf("Cleaning up disk targets...\n");
    execute_cmd("swapoff -a 2>/dev/null");
    snprintf(cmd, sizeof(cmd), "umount -f %s* 2>/dev/null", disk);
    execute_cmd(cmd);
    
    snprintf(cmd, sizeof(cmd), "wipefs -a %s", disk);
    execute_cmd_abort(cmd, "Failed to wipe existing filesystem signatures");

    printf("Partitioning disk %s...\n", disk);
    snprintf(cmd, sizeof(cmd), "parted -s %s mklabel gpt", disk);
    execute_cmd_abort(cmd, "Failed to create GPT label");

    snprintf(cmd, sizeof(cmd), "parted -s %s mkpart ESP fat32 1MiB 513MiB", disk);
    execute_cmd_abort(cmd, "Failed to create EFI partition");

    snprintf(cmd, sizeof(cmd), "parted -s %s set 1 boot on", disk);
    execute_cmd_abort(cmd, "Failed to set boot flag");

    snprintf(cmd, sizeof(cmd), "parted -s %s mkpart root ext4 513MiB 100%%", disk);
    execute_cmd_abort(cmd, "Failed to create Root partition");

    snprintf(cmd, sizeof(cmd), "partprobe %s", disk);
    execute_cmd(cmd);
    execute_cmd("udevadm settle");
    sleep(2);

    printf("Formatting partitions...\n");
    snprintf(cmd, sizeof(cmd), "mkfs.vfat -F32 %s", p1);
    execute_cmd_abort(cmd, "Failed to format EFI partition");

    snprintf(cmd, sizeof(cmd), "mkfs.ext4 -F %s", p2);
    execute_cmd_abort(cmd, "Failed to format Root partition");
}

int main(void) {
    setenv("LC_ALL", "en_US.UTF-8", 1);
    setenv("LANG", "en_US.UTF-8", 1);

    ensure_network();

    int is_uefi = check_uefi();
    printf("Boot Mode: %s\n", is_uefi ? "UEFI" : "Legacy BIOS");

    char disk[128];
    select_disk(disk, sizeof(disk));

    char hostname[] = "exherbo";
    char root_pass[128], username[64], user_pass[128];
    char p1[128], p2[128], cmd[1024];

    get_string("Enter Root Password: ", root_pass, sizeof(root_pass));
    get_string("Enter Username: ", username, sizeof(username));
    get_string("Enter User Password: ", user_pass, sizeof(user_pass));

    printf("\n=======================================================\n");
    printf("All parameters collected successfully!\n");
    printf("Starting fully automated installation. Do not interrupt.\n");
    printf("=======================================================\n\n");

    partition_disk(disk, p1, p2, sizeof(p2));

    printf("Mounting partitions...\n");
    execute_cmd("mkdir -p /mnt/exherbo");
    snprintf(cmd, sizeof(cmd), "mount %s /mnt/exherbo", p2);
    execute_cmd_abort(cmd, "Failed to mount Root partition");

    execute_cmd("mkdir -p /mnt/exherbo/boot");
    snprintf(cmd, sizeof(cmd), "mount %s /mnt/exherbo/boot", p1);
    execute_cmd_abort(cmd, "Failed to mount EFI partition");

    printf("Fetching dynamic Exherbo stage URL...\n");
    FILE *pipe = popen("curl -s https://stages.exherbo.org/x86_64-pc-linux-gnu/ | grep -o 'exherbo-x86_64-pc-linux-gnu-gcc-[0-9]*\\.tar\\.xz' | sort | tail -n 1", "r");
    char stage_file[256] = {0};
    if (pipe) {
        fgets(stage_file, sizeof(stage_file), pipe);
        pclose(pipe);
    }
    
    size_t len = strlen(stage_file);
    if (len > 0 && stage_file[len-1] == '\n') {
        stage_file[len-1] = '\0';
    }

    if (strlen(stage_file) == 0) {
        printf("FATAL ERROR: Failed to parse Exherbo stage filename.\n");
        exit(1);
    }

    snprintf(cmd, sizeof(cmd), "curl -fSLO https://stages.exherbo.org/x86_64-pc-linux-gnu/%s", stage_file);
    execute_cmd_abort(cmd, "Failed to download Exherbo stage");

    printf("Unpacking Stage...\n");
    snprintf(cmd, sizeof(cmd), "tar xJpf %s -C /mnt/exherbo", stage_file);
    execute_cmd_abort(cmd, "Failed to extract Exherbo stage");
    snprintf(cmd, sizeof(cmd), "rm -f %s", stage_file);
    execute_cmd(cmd);

    printf("Copying DNS configuration...\n");
    execute_cmd_abort("cp --dereference /etc/resolv.conf /mnt/exherbo/etc/", "Failed to copy DNS info");

    printf("Mounting virtual filesystems...\n");
    execute_cmd_abort("mount --types proc /proc /mnt/exherbo/proc", "Failed to mount proc");
    execute_cmd_abort("mount --rbind /sys /mnt/exherbo/sys && mount --make-rslave /mnt/exherbo/sys", "Failed to mount sys");
    execute_cmd_abort("mount --rbind /dev /mnt/exherbo/dev && mount --make-rslave /mnt/exherbo/dev", "Failed to mount dev");
    execute_cmd_abort("mount --bind /run /mnt/exherbo/run && mount --make-slave /mnt/exherbo/run", "Failed to mount run");

    FILE *script = fopen("/mnt/exherbo/setup_chroot.sh", "w");
    if (!script) {
        printf("FATAL ERROR: Failed to create chroot script.\n");
        exit(1);
    }

    fprintf(script, "#!/bin/bash\nset -e\nsource /etc/profile\n\n");

    fprintf(script, "ROOT_UUID=$(blkid -s UUID -o value %s)\n", p2);
    fprintf(script, "EFI_UUID=$(blkid -s UUID -o value %s)\n", p1);
    fprintf(script, "echo \"UUID=${ROOT_UUID} / ext4 defaults 0 1\" > /etc/fstab\n");
    fprintf(script, "echo \"UUID=${EFI_UUID} /boot vfat defaults 0 2\" >> /etc/fstab\n");

    fprintf(script, "mkdir -p /etc/paludis/options.conf.d\n");
    fprintf(script, "echo \"*/* efi systemd\" > /etc/paludis/options.conf.d/efi.conf\n");

    fprintf(script, "cave sync\n");

    fprintf(script, "echo \"127.0.0.1 localhost %s\" > /etc/hosts\n", hostname);
    fprintf(script, "echo \"::1 localhost %s\" >> /etc/hosts\n", hostname);
    fprintf(script, "echo \"%s\" > /etc/hostname\n", hostname);
    fprintf(script, "systemd-machine-id-setup || true\n");

    fprintf(script, "localedef -i en_US -f UTF-8 en_US.UTF-8 || true\n");
    fprintf(script, "echo 'LANG=\"en_US.UTF-8\"' > /etc/env.d/99locale\n");
    fprintf(script, "ln -sf /usr/share/zoneinfo/UTC /etc/localtime\n");

    fprintf(script, "cave resolve --execute --preserve-world --skip-phase test sys-apps/systemd\n");

    fprintf(script, "cave resolve -x sys-kernel/linux-firmware sys-boot/dracut sys-boot/grub net-misc/networkmanager dev-vcs/git curl wget sys-apps/pciutils sys-devel/bc sys-devel/bison sys-devel/flex dev-libs/elfutils\n");
    fprintf(script, "systemctl enable NetworkManager\n");

    fprintf(script, "cd /usr/src\n");
    fprintf(script, "KVER=$(curl -s https://www.kernel.org/releases.json | grep -m1 '\"version\":' | grep -o '[0-9]\\+\\.[0-9]\\+\\.[0-9]\\+')\n");
    fprintf(script, "KMAJ=${KVER%%.*}\n");
    fprintf(script, "wget -q \"https://cdn.kernel.org/pub/linux/kernel/v$KMAJ.x/linux-$KVER.tar.xz\"\n");
    fprintf(script, "tar xf linux-$KVER.tar.xz\n");
    fprintf(script, "cd linux-$KVER\n");

    fprintf(script, "make defconfig\n");
    fprintf(script, "scripts/config --enable CONFIG_BLK_DEV_NVME\n");
    fprintf(script, "scripts/config --enable CONFIG_EFI_STUB\n");
    fprintf(script, "scripts/config --enable CONFIG_EFI\n");
    fprintf(script, "scripts/disable CONFIG_DEBUG_INFO_BTF\n");
    fprintf(script, "make olddefconfig\n");
    fprintf(script, "make -j$(nproc)\n");
    fprintf(script, "make modules_install\n");
    fprintf(script, "cp arch/x86/boot/bzImage /boot/vmlinuz-$KVER\n");

    fprintf(script, "dracut --force /boot/initramfs-$KVER.img $KVER\n");

    if (is_uefi) {
        fprintf(script, "bootctl install --path=/boot\n");
        fprintf(script, "mkdir -p /boot/loader/entries\n");
        fprintf(script, "cat <<LENT > /boot/loader/entries/exherbo.conf\n");
        fprintf(script, "title Exherbo Linux\n");
        fprintf(script, "linux /vmlinuz-$KVER\n");
        fprintf(script, "initrd /initramfs-$KVER.img\n");
        fprintf(script, "options root=UUID=${ROOT_UUID} rw init=/usr/lib/systemd/systemd\n");
        fprintf(script, "LENT\n");
        fprintf(script, "cat <<LCONF > /boot/loader/loader.conf\n");
        fprintf(script, "default exherbo.conf\n");
        fprintf(script, "timeout 3\n");
        fprintf(script, "LCONF\n");
    } else {
        fprintf(script, "grub-install %s\n", disk);
        fprintf(script, "grub-mkconfig -o /boot/grub/grub.cfg\n");
    }

    fprintf(script, "useradd -m -G adm,disk,wheel,cdrom,audio,video,usb,users %s\n", username);
    fprintf(script, "echo \"root:%s\" | chpasswd\n", root_pass);
    fprintf(script, "echo \"%s:%s\" | chpasswd\n", username, user_pass);

    fprintf(script, "su - %s -c 'mkdir -p ~/git && git clone https://github.com/gh0st-8221/ghostwm ~/git/ghostwm'\n", username);

    fclose(script);

    execute_cmd("chmod +x /mnt/exherbo/setup_chroot.sh");

    printf("Executing setup inside chroot...\n");
    execute_cmd_abort("chroot /mnt/exherbo /setup_chroot.sh", "Chroot setup script failed");

    printf("Cleaning up...\n");
    execute_cmd("rm -f /mnt/exherbo/setup_chroot.sh");
    execute_cmd("umount -l /mnt/exherbo/dev/pts 2>/dev/null");
    execute_cmd("umount -l /mnt/exherbo/dev/shm 2>/dev/null");
    execute_cmd("umount -R /mnt/exherbo 2>/dev/null");

    printf("\n=======================================================\n");
    printf("Exherbo Linux installation completed successfully!\n");
    printf("You can now safely reboot your system.\n");
    printf("=======================================================\n");

    return 0;
}
