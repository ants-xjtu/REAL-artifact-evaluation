extern int ioctl_(int ufd, unsigned long request, char *argp);

extern "C" __attribute__((visibility("default"))) int ioctl(int fd, unsigned long request, char *argp)
{
    return ioctl_(fd, request, argp);
}
