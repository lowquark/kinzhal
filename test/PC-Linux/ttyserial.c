
#include <errno.h>
#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>
#include <stdint.h>
#include <stdlib.h>

#include "kinzhal.h"

int set_interface_attribs(int fd, int speed, int parity) {
  struct termios tty;
  memset(&tty, 0, sizeof tty);
  if(tcgetattr(fd, &tty) != 0)
  {
    fprintf(stderr, "error %d from tcgetattr", errno);
    return -1;
  }

  cfsetospeed(&tty, speed);
  cfsetispeed(&tty, speed);

  tty.c_cflag = (tty.c_cflag & ~CSIZE) | CS8;     // 8-bit chars
  // disable IGNBRK for mismatched speed tests; otherwise receive break
  // as \000 chars
  tty.c_iflag &= ~IGNBRK;         // disable break processing
  tty.c_lflag = 0;                // no signaling chars, no echo,
                                  // no canonical processing
  tty.c_oflag = 0;                // no remapping, no delays
  tty.c_cc[VMIN]  = 0;            // read doesn't block
  tty.c_cc[VTIME] = 5;            // 0.5 seconds read timeout

  tty.c_iflag &= ~(IXON | IXOFF | IXANY); // shut off xon/xoff ctrl

  tty.c_cflag |= (CLOCAL | CREAD);// ignore modem controls,
                                  // enable reading
  tty.c_cflag &= ~(PARENB | PARODD);      // shut off parity
  tty.c_cflag |= parity;
  tty.c_cflag &= ~CSTOPB;
  tty.c_cflag &= ~CRTSCTS;

  if(tcsetattr(fd, TCSANOW, &tty) != 0)
  {
    fprintf(stderr, "error %d from tcsetattr", errno);
    return -1;
  }
  return 0;
}

void set_blocking(int fd, int should_block) {
  struct termios tty;
  memset(&tty, 0, sizeof tty);
  if(tcgetattr(fd, &tty) != 0)
  {
    fprintf(stderr, "error %d from tggetattr", errno);
    return;
  }

  tty.c_cc[VMIN]  = should_block ? 1 : 0;
  tty.c_cc[VTIME] = 0;            // 0.0 seconds read timeout

  if(tcsetattr(fd, TCSANOW, &tty) != 0) {
    fprintf(stderr, "error %d setting term attributes", errno);
  }
}


typedef struct {
  int fd;
  kz_byte_t rx_buffer[256];
  kz_byte_t tx_buffer[256];
} tty_port;


tty_port port;


int port_rx(kz_byte_t * byte) {
  char c;
  int ret = read(port.fd, &c, 1);

  if(ret <= 0) {
    return 0;
  } else {
    *byte = c;
    return 1;
  }
}

void port_tx(const kz_byte_t * buffer, size_t buffer_size) {
  write(port.fd, buffer, buffer_size);
}


void loopcount_handler(kz_endpoint_t * K, void * userdata, kz_request_status_t status) {
  int * request_id = userdata;

  if(status == KZ_IGNORE) {
    printf("Arduino ignored us! (request id: %d)\n", *request_id);
  } else {
    kz_int_t loop_count;
    if(kz_getint(K, &loop_count)) {
      printf("Arduino loop count: %ld (request id: %d)\n", loop_count, *request_id);
    } else {
      printf("Error reading loop count. (request id: %d)\n", *request_id);
    }
  }

  free(userdata);
}


kz_endpoint_t endpoint;

static void mainloop() {
  sleep(2);

  // initialize endpoint
  kz_endpointdef_t def;
  def.rx_buffer = port.rx_buffer;
  def.rx_buffer_size = sizeof(port.rx_buffer);
  def.tx_buffer = port.tx_buffer;
  def.tx_buffer_size = sizeof(port.tx_buffer);
  def.rx = port_rx;
  def.tx = port_tx;

  kz_init_static(&endpoint, &def);

  // request the arduino's loop count... constantly
  int request_id = 0;

  while(1) {
    printf("Requesting loop count... (request id: %d)\n", request_id);

    //kz_send(&endpoint, 1);

    // allocate call data containing this particular request id
    int * call_data = malloc(sizeof(*call_data));
    *call_data = request_id;

    // make request
    kz_call(&endpoint, 4, loopcount_handler, call_data, 100);

    request_id ++;

    usleep(20000);

    // process pending rx data, timeouts
    kz_tick(&endpoint);
  }
}

int main(int argc, char ** argv) {
  if(argc >= 2) {
    const char * portname = argv[1];

    port.fd = open(portname, O_RDWR | O_NOCTTY | O_SYNC);
    if(port.fd < 0) {
      fprintf(stderr, "error %d opening %s: %s", errno, portname, strerror(errno));
      return 1;
    }

    set_interface_attribs(port.fd, B115200, 0);
    set_blocking(port.fd, 0);

    mainloop();
  } else if(argc >= 1) {
    fprintf(stderr, "usage: %s <device>\n", argv[0]);
  }

  return 0;
}

