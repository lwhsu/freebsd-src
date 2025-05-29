#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/ioccom.h> // For _IOW
#include <errno.h>
#include <err.h> // For err() and warn()

// Assuming MAX_NBR_WTAP from if_wtapvar.h is 64 for userspace test
#define MAX_NBR_WTAP 64

// Copied from sys/dev/wtap/plugins/visibility_ioctl.h
struct vis_distance_link {
	uint16_t id1; /* ID of the first node */
	uint16_t id2; /* ID of the second node */
	float distance; /* The distance between id1 and id2 */
};

// Definition from sys/dev/wtap/plugins/visibility_ioctl.h
// #define VISIOCTLSETDISTANCE _IOW('W', 5, struct vis_distance_link)
// 'W' is 0x57 in ASCII. So, the command number can be constructed.
// However, it's better to rely on _IOW if sys/ioccom.h is available.
#define VISIOCTLSETDISTANCE _IOW('W', 5, struct vis_distance_link)

int main() {
    int fd;
    struct vis_distance_link dist_link_data;
    int ret;
    int overall_success = 1; // 1 for success, 0 for failure

    fd = open("/dev/visctl", O_RDWR);
    if (fd < 0) {
        err(EXIT_FAILURE, "Failed to open /dev/visctl");
    }

    printf("Starting VISIOCTLSETDISTANCE tests...\n");

    // Test Case 1: Set a valid distance
    printf("\nTest Case 1: Set valid distance (id1=0, id2=1, distance=10.5f)\n");
    dist_link_data.id1 = 0;
    dist_link_data.id2 = 1;
    dist_link_data.distance = 10.5f;
    ret = ioctl(fd, VISIOCTLSETDISTANCE, &dist_link_data);
    if (ret == -1) {
        perror("ioctl VISIOCTLSETDISTANCE (Test Case 1)");
        printf("Test Case 1: FAILED\n");
        overall_success = 0;
    } else {
        printf("Test Case 1: PASSED (ioctl returned %d)\n", ret);
    }

    // Test Case 2: Invalid id1
    printf("\nTest Case 2: Invalid id1 (id1=MAX_NBR_WTAP, id2=0, distance=5.0f)\n");
    dist_link_data.id1 = MAX_NBR_WTAP; // Invalid
    dist_link_data.id2 = 0;
    dist_link_data.distance = 5.0f;
    ret = ioctl(fd, VISIOCTLSETDISTANCE, &dist_link_data);
    if (ret == -1 && errno == EINVAL) {
        printf("Test Case 2: PASSED (ioctl returned -1, errno=EINVAL as expected)\n");
    } else if (ret == -1) {
        perror("ioctl VISIOCTLSETDISTANCE (Test Case 2) - unexpected errno");
        printf("Test Case 2: FAILED (errno was %d, expected EINVAL)\n", errno);
        overall_success = 0;
    } else {
        printf("Test Case 2: FAILED (ioctl did not return -1)\n");
        overall_success = 0;
    }
    errno = 0; // Reset errno

    // Test Case 3: Invalid id2
    printf("\nTest Case 3: Invalid id2 (id1=0, id2=MAX_NBR_WTAP, distance=5.0f)\n");
    dist_link_data.id1 = 0;
    dist_link_data.id2 = MAX_NBR_WTAP; // Invalid
    dist_link_data.distance = 5.0f;
    ret = ioctl(fd, VISIOCTLSETDISTANCE, &dist_link_data);
    if (ret == -1 && errno == EINVAL) {
        printf("Test Case 3: PASSED (ioctl returned -1, errno=EINVAL as expected)\n");
    } else if (ret == -1) {
        perror("ioctl VISIOCTLSETDISTANCE (Test Case 3) - unexpected errno");
        printf("Test Case 3: FAILED (errno was %d, expected EINVAL)\n", errno);
        overall_success = 0;
    } else {
        printf("Test Case 3: FAILED (ioctl did not return -1)\n");
        overall_success = 0;
    }
    errno = 0; // Reset errno

    // Test Case 4: Negative distance
    printf("\nTest Case 4: Negative distance (id1=0, id2=1, distance=-5.0f)\n");
    dist_link_data.id1 = 0;
    dist_link_data.id2 = 1;
    dist_link_data.distance = -5.0f; // Invalid
    ret = ioctl(fd, VISIOCTLSETDISTANCE, &dist_link_data);
    if (ret == -1 && errno == EINVAL) {
        printf("Test Case 4: PASSED (ioctl returned -1, errno=EINVAL as expected)\n");
    } else if (ret == -1) {
        perror("ioctl VISIOCTLSETDISTANCE (Test Case 4) - unexpected errno");
        printf("Test Case 4: FAILED (errno was %d, expected EINVAL)\n", errno);
        overall_success = 0;
    } else {
        printf("Test Case 4: FAILED (ioctl did not return -1)\n");
        overall_success = 0;
    }
    errno = 0; // Reset errno

    if (close(fd) == -1) {
        perror("Failed to close /dev/visctl");
        // No change to overall_success as tests might have passed/failed already
    }

    printf("\nOverall test result: %s\n", overall_success ? "PASSED" : "FAILED");
    return overall_success ? 0 : 1;
}
