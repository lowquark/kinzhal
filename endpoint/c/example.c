
#include <kazhal.h>
#include <stdio.h>

kz_request_status_t do_thing_handler(kz_endpoint_t * K, void * _) {
  kz_float_t a, b;

  printf("do_thing_handler()\n");

  if(!kz_getnumber(K, &a)) {
    return KZ_INVALID;
  }
  if(!kz_getnumber(K, &b)) {
    return KZ_INVALID;
  }

  kz_putfloat(K, a + b);
  kz_putfloat(K, a * b);

  return KZ_OK;
}

kz_request_status_t get_thing_handler(kz_endpoint_t * K, void * _) {
  printf("get_thing_handler()\n");

  kz_putfloat(K, 3.14159f);
  kz_putfloat(K, 2.71828f);

  return KZ_OK;
}

void endpoint_tx(const kz_byte_t * bytes, size_t size) {
  size_t i;

  printf("tx: [ ");
  for(i = 0 ; i < size ; i ++) {
    printf("%02X ", (uint8_t)bytes[i]);
  }
  printf("]\n");
}

int endpoint_rx(kz_byte_t * byte) {
  static int i = 0;

  static const kz_byte_t bytes[] = {
    0x04, 0x50, 0x01, 0x01, 0x05, 0xFF, 0x89, 0x05, 0x05, 0x00,
    0x04, 0x50, 0x02, 0x02, 0x01, 0x00,
  };

  if(i < sizeof(bytes)) {
    *byte = bytes[i];
    i ++;
    return 1;
  }

  return 0;
}

kz_endpoint_t endpoint;
kz_byte_t tx_buf[KZ_MAX_BUFFER_SIZE];
kz_byte_t rx_buf[KZ_MAX_BUFFER_SIZE];

int main(int argc, char ** argv) {
  kz_endpointdef_t def;
  def.rx_buffer      = rx_buf;
  def.rx_buffer_size = sizeof(rx_buf);
  def.tx_buffer      = tx_buf;
  def.tx_buffer_size = sizeof(tx_buf);
  def.rx = endpoint_rx;
  def.tx = endpoint_tx;

  kz_init_static(&endpoint, &def);

  kz_handle(&endpoint, 1, do_thing_handler, NULL);
  kz_handle(&endpoint, 2, get_thing_handler, NULL);


  /* place a float on the put buffer */
  kz_putfloat(&endpoint, 5.439);
  /* transmit a request on channel 0x00 with everything in the put buffer
   * calls fnfnfn when a reply has been received
   *
   * kz_call(&endpoint, 0x00, fnfnfn, NULL, 100);
   */
  kz_send(&endpoint, 0x00);


  endpoint.rx = endpoint_rx;
  endpoint.tx = endpoint_tx;

  kz_tick(&endpoint);

  return 0;
}

