#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <sched.h>
#include <unistd.h>
#include <sys/mount.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <dirent.h>

#define STACK_SIZE (1024 * 1024) // 栈大小

char child_stack[STACK_SIZE];

void listFilesInDirectory(const char *directory_path) {
    DIR *dir = opendir(directory_path);
    if (dir == NULL) {
        perror("opendir");
        return;
    }

    struct dirent *entry;

    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_type == DT_REG) {
            printf("File: %s\n", entry->d_name);
        } else if (entry->d_type == DT_DIR) {
            printf("Directory: %s\n", entry->d_name);
        }
    }

    closedir(dir);
}

int child_function(void *arg) {
    //使用私有挂载保证私密性，系统用户将无法看到当前进程使用tempfs挂载信息
    fprintf(stderr, "=> remounting everything with MS_PRIVATE...");
    if (mount(NULL, "/", NULL, MS_REC | MS_PRIVATE, NULL)) {
        fprintf(stderr, "failed! %m\n");
        return -1;
    }
    fprintf(stderr, "done! %m\n");

    //创建临时目录，并挂载tempfs
    char template[] = "/tmp/mytempdirXXXXXX";
    char *temp_dir = mkdtemp(template);
    if (temp_dir != NULL) {
        printf("Created temporary directory: %s\n", temp_dir);
        // 在这里可以在临时目录中执行操作
    } else {
        perror("mkdtemp");
    }
    if (mount("none", temp_dir, "tmpfs", 0, "size=100M") == -1) {
        perror("mount");
        return 1;
    }
    char root_dir[128];
    char root_img[] = "/home/ubuntu/CLionProjects/contained/ubuntu.img";
    sprintf(root_dir, "%s/%s", temp_dir, "root");
    mkdir(root_dir, 0777);
    printf("root_dir %s \n", root_dir);

    //挂载busybox的rootfs镜像 直接mount不支持img文件，用命令实现
//    if (mount(root_img, root_dir, "ext4", 0, NULL) == -1) {
//        perror("mount");
//        return 1;
//    }
    char cmd[1024];
    sprintf(cmd, "mount %s %s", root_img, root_dir);
    int status = system(cmd);

    if (status == -1) {
        perror("system");
        return 1;
    } else {
        if (WIFEXITED(status)) {
            int exit_status = WEXITSTATUS(status);
            printf("Command exited with status: %d\n", exit_status);
        } else {
            printf("Command did not exit normally.\n");
        }
    }
    char sysDir[1024];
    sprintf(sysDir, "%s/%s", root_dir, "sys");
    if (mount("sys", sysDir, "sysfs", 0, NULL) == 0) {
        perror(" mount proc fail");
    }
    char procDir[1024];
    sprintf(procDir, "%s/%s", root_dir, "proc");
    if (mount("proc", procDir, "proc", 0, NULL) == 0) {
        perror(" mount proc fail");
    }

    char devDir[1024];
    sprintf(devDir, "%s/%s", root_dir, "dev");
    if (mount("/dev", devDir, NULL, MS_BIND, "") == -1) {
        perror(" mount dev fail");
    }

    char devPtsDir[1024];
    sprintf(devPtsDir, "%s/%s", root_dir, "dev/pts");
    mkdir(devPtsDir, 0777);
    if (mount("/dev/pts", devPtsDir, NULL, MS_BIND, "") == -1) {
        perror(" mount /dev/pts  fail");
    }

    if (chroot(root_dir) == -1) {
        perror("chroot");
        return 1;
    }
    // 切换工作目录
    if (chdir("/") == -1) {
        perror("chdir");
        return 1;
    }
    //启动ubuntu img中的sh命令
    execvp("sh", NULL);
    umount2(procDir, MNT_DETACH);
    umount2(devPtsDir, MNT_DETACH);
    umount2(devDir, MNT_DETACH);
    umount2(root_dir, MNT_DETACH);
    if (umount2(temp_dir, MNT_DETACH)) {
        fprintf(stderr, "umount failed! %m\n");
        return -1;
    }
    if (rmdir(temp_dir)) {
        fprintf(stderr, "rmdir failed! %m\n");
        return -1;
    }
    return 0;
}

int main() {
    // 创建新的命名空间
    int flags = CLONE_NEWUTS | CLONE_NEWPID | CLONE_NEWNS;
    pid_t child_pid = clone(child_function, child_stack + STACK_SIZE, flags | SIGCHLD, NULL);

    if (child_pid == -1) {
        perror("clone");
        return 1;
    }

    // 等待子进程退出
    waitpid(child_pid, NULL, 0);

    return 0;
}
