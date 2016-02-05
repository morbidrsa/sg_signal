/*
 * (C) 2016 SUSE Linux GmbH, Johannes Thumshirn <jthumshirn@suse.de>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>

#include <scsi/sg.h>

#include <pthread.h>
#include <stdlib.h>
#include <stdio.h>
#include <signal.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#define DEV_NAME_MAX 255
#define READ16_REPLY_LEN 512
#define READ16_CMD_LEN 16

#define __unused __attribute__((unused))

struct config {
	char devname[DEV_NAME_MAX];
};

static void sighand(int __unused signo)
{
	printf(".");
	fflush(stdout);

	return;
}

static bool is_zero_reply(struct sg_io_hdr *hdr)
{
	unsigned int i;
	unsigned char *dxferp;

	dxferp = hdr->dxferp;

	for (i = 0; i <= hdr->dxfer_len; i++) {
		if (dxferp[i] != 0)
			return false;
	}

	return true;
}

void hexdump(void *dp, unsigned int len)
{
	unsigned char *dxferp;
	unsigned int i;

	dxferp = dp;

	for (i = 0; i < len; i++) {
		if ((i % 8) == 0) {
			if (i != 0) 
				printf("\n");
			printf("%04x ", i);
		}

		printf(" 0x%02x", dxferp[i]);
	}
}

/* do scsi SG_IO stuff here */
static void *work(void *arg)
{
	unsigned char r16cmd[READ16_CMD_LEN] = 
		{ 0x88, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0 };
	unsigned char reply_buffer[READ16_REPLY_LEN];
	unsigned char sense_buffer[32];
	struct config *config = arg;
	struct sg_io_hdr hdr;
	int fd;
	int rc;

	fd = open(config->devname, O_RDWR);
	if (fd < 0) {
		perror("open");
		return NULL;
	}

	memset(&hdr, 0, sizeof(struct sg_io_hdr));
	hdr.interface_id = 'S';
	hdr.cmd_len = sizeof(r16cmd);
	//hdr.max_sb_len = sizeof(sense_buffer);
	hdr.dxfer_direction = SG_DXFER_FROM_DEV;
	hdr.dxfer_len = READ16_REPLY_LEN;
	hdr.dxferp = reply_buffer;
	hdr.cmdp = r16cmd;
	hdr.sbp = sense_buffer;
	hdr.timeout = 20000;

	srand(time(NULL));
	/* main work thread */
	while (1) {
		sleep(rand() % 3);

		rc = ioctl(fd, SG_IO, &hdr);
		if (rc < 0 && errno != EINTR) {
			perror("ioctl");
			goto out_close;
		}

		if (hdr.dxfer_len == 0)
			continue;

		rc = is_zero_reply(&hdr);
		if (rc) {
			fprintf(stderr, "Got READ16 reply of all 0\n");
			hexdump(hdr.dxferp, hdr.dxfer_len);
			goto out_close;
		}
	}

out_close:
	close(fd);
	return NULL;
}

int main(int argc, char **argv)
{
	struct sigaction actions;
	struct config config;
	pthread_t worker;
	int rc;

	if (argc < 2) {
		fprintf(stderr, "Usage: %s <scsi_device>\n", argv[0]);
		exit(EXIT_FAILURE);
	}

	strncpy(config.devname, argv[1], DEV_NAME_MAX);

	/* Setup signal handler */
	memset(&actions, 0, sizeof(actions));
	sigemptyset(&actions.sa_mask);
	actions.sa_flags = 0;
	actions.sa_handler= sighand;

	rc = sigaction(SIGUSR1, &actions, NULL);
	if (rc < 0) {
		perror("sigaction");
		exit(EXIT_FAILURE);
	}

	/* Create SCSI worker thread */
	rc = pthread_create(&worker, NULL, work, &config);
	if (rc < 0) {
		perror("pthread_create");
		exit(EXIT_FAILURE);
	}

	srand(time(NULL));

	/* poke SCSI worker, constantly. */
	while (1) {
		sleep(rand() % 3);

		rc = pthread_kill(worker, SIGUSR1);
		if (rc) {
			if (errno)
				perror("pthread_kill");
			break;
		}
	}

	pthread_join(worker, NULL);

	return 0;
}
