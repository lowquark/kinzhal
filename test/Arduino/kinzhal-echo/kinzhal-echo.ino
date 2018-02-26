
#include "kinzhal.hpp"

void tx_Serial(const kz_byte_t * b, kz_size_t size) {
  Serial.write(b, size);
}
int rx_Serial(kz_byte_t * b) {
  if(Serial.available()) {
    *b = Serial.read();
    return 1;
  } else {
    return 0;
  }
}

int loop_count = 0;

// Pi!
static kz_request_status_t get_pi(kz_endpoint_t * K, void * _) {
  kz_putfloat(K, 3.14159f);
  return KZ_OK;
}
// Euler's number!
static kz_request_status_t get_eulers(kz_endpoint_t * K, void * _) {
  kz_putfloat(K, 2.71828f);
  return KZ_OK;
}
// A three-tuple!
static kz_request_status_t get_three(kz_endpoint_t * K, void * _) {
  kz_putlistopen(K);
  kz_putint(K, 5);
  kz_putint(K, 5);
  kz_putint(K, 5);
  kz_putlistclose(K);
  return KZ_OK;
}
// The loop count!
static kz_request_status_t get_numloops(kz_endpoint_t * K, void * _) {
  kz_putint(K, loop_count);
  return KZ_OK;
}

// Blink n times!
static kz_request_status_t handle_blink(kz_endpoint_t * K, void * _) {
  kz_int_t n;
  if(kz_getint(K, &n)) {
    for(int i = 0 ; i < n ; i ++) {
      digitalWrite(13, HIGH);
      delay(100);
      digitalWrite(13, LOW);
      delay(100);
    }
    delay(500);
    return KZ_OK;
  } else {
    return KZ_INVALID;
  }
}

/*
static const char * const channel_info[] = {
        "pi () (float)"         ,
    "eulers () (float)"         ,
     "three () ((int int int))" ,
  "numloops () (int)"           ,
     "blink (int) ()"           ,
};

void send_info(kze_request_t req) {
  kz_writer_t W;
  kz_string_t infostr;

  W = kz_writerb(byte_train);

  // send info for each channel
  for(unsigned int i = 0 ; i < sizeof(channel_info)/sizeof(channel_info[0]) ; i ++) {
    infostr.bytes = (const kz_byte_t *)channel_info[i];
    infostr.length = strlen(channel_info[i]);

    kz_writeint(&W, i);
    kz_writestring(&W, infostr);

    kze_sendreply(req, kz_stringw(&W));
  }

  infostr.bytes = NULL;
  infostr.length = 0;
  kze_sendreply(req, infostr);
}
*/

kz_byte_t rx_buffer[16];
kz_byte_t tx_buffer[16];
kz_endpoint_t endpoint;
kz_endpoint_t * const K = &endpoint;

void setup() {
  Serial.begin(115200);

  pinMode(13, OUTPUT);

  kz_endpointdef_t def;
  def.rx_buffer = rx_buffer;
  def.rx_buffer_size = sizeof(rx_buffer);
  def.tx_buffer = tx_buffer;
  def.tx_buffer_size = sizeof(tx_buffer);
  def.rx = rx_Serial;
  def.tx = tx_Serial;

  kz_init_static(K, &def);

  kz_handle(K, 1, get_pi, NULL);
  kz_handle(K, 2, get_eulers, NULL);
  kz_handle(K, 3, get_three, NULL);
  kz_handle(K, 4, get_numloops, NULL);
  kz_handle(K, 5, handle_blink, NULL);
}

void loop() {
  kz_tick(K);
  loop_count ++;
  delay(10);
}

