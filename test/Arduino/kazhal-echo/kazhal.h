#ifndef KAZHAL_H
#define KAZHAL_H


/* begin configuration */

#include <stdint.h>
#include <stddef.h>
#include <assert.h>

typedef uint8_t kz_byte_t;
typedef float   kz_float_t;
typedef int     kz_int_t;
typedef size_t  kz_size_t;

#define KZ_MAX_FOREIGN_REQUESTS  16
#define KZ_MAX_LOCAL_REQUESTS    16
#define KZ_MAX_CHANNELS          32

#define KZ_ASSERT            assert

/* end configuration */


#define KZ_MIN_BUFFER_SIZE        16
#define KZ_MAX_BUFFER_SIZE       256


typedef struct kz_string {
  const kz_byte_t * bytes;
  kz_size_t length;
} kz_string_t;

typedef enum kz_request_status {
  KZ_IGNORE,
  KZ_INVALID,
  KZ_BUSY,
  KZ_OK
} kz_request_status_t;


struct kz_endpoint;

/* blocking transmit function */
typedef void (* kz_txhandlerfn_t) (const kz_byte_t * bytes, size_t size);

/* non-blocking receive function */
typedef int  (* kz_rxhandlerfn_t) (kz_byte_t * byte);

/* foreign call handler */
typedef kz_request_status_t (* kz_request_handler_fn_t)(struct kz_endpoint * K,
                                                        void * userdata);

/* called when a call has finished */
typedef void (* kz_reply_handler_fn_t)(struct kz_endpoint * K,
                                       void * userdata,
                                       kz_request_status_t status);


typedef struct kz_request {
  kz_byte_t foreign_id;
  char active;
} kz_request_t;

typedef struct kz_local_request {
  kz_reply_handler_fn_t callback;
  void * userdata;
  int timeout_ticks;
} kz_local_request_t;

typedef struct kz_request_handler {
  kz_request_handler_fn_t callback;
  void * userdata;
} kz_request_handler_t;

/* Possible states when decoding COBS */
typedef enum {
  KZ_RX_IDLE,
  KZ_RX_ABORT,
  KZ_RX_DECODE
} kz_cobs_rx_state_t;

typedef struct kz_endpointdef {
  kz_byte_t * rx_buffer;     /* Receive buffer to be used by the endpoint */
  kz_byte_t * tx_buffer;     /* Transmit buffer to be used by the endpoint */

  kz_rxhandlerfn_t rx;       /* Receive callback */
  kz_txhandlerfn_t tx;       /* Transmit callback */

  kz_size_t rx_buffer_size;  /* Size of given receive buffer in bytes */
  kz_size_t tx_buffer_size;  /* Size of given transmit buffer in bytes */
} kz_endpointdef_t;

typedef struct kz_endpoint {
  kz_byte_t * rx_buffer;     /* Beginning of receive buffer */
  kz_byte_t * rx_buffer_pos; /* Past-end pointer of received frame */
  kz_byte_t * rx_buffer_end; /* Past-end pointer of receive buffer */

  kz_byte_t * tx_buffer;     /* Beginning of transmit buffer */
  kz_byte_t * tx_buffer_end; /* Past-end pointer of transmit buffer */

  kz_byte_t * getptr;        /* Pointer to next byte to decode */
  kz_byte_t * putptr;        /* Pointer to next byte to encode */

  kz_rxhandlerfn_t rx;
  kz_txhandlerfn_t tx;

  /* indexed by channel id */
  kz_request_handler_t handlers[KZ_MAX_CHANNELS];

  /* pool for current local requests */
  kz_local_request_t local_requests[KZ_MAX_LOCAL_REQUESTS];

  kz_cobs_rx_state_t rx_state;
  unsigned int       rx_count;
} kz_endpoint_t;


void kz_init_static(kz_endpoint_t * K,
                    const kz_endpointdef_t * def);

int kz_handle(kz_endpoint_t * K,
              unsigned int channelid,
              kz_request_handler_fn_t fn,
              void * userdata);

void kz_tick(kz_endpoint_t * K);

int kz_call(kz_endpoint_t * K, unsigned int channelid,
            kz_reply_handler_fn_t fn, void * userdata, int timeout_ticks);

void kz_send(kz_endpoint_t * K, unsigned int channelid);

/* receive data from the rx buffer */
int  kz_getint(kz_endpoint_t * K, kz_int_t * i);
int  kz_getfloat(kz_endpoint_t * K, kz_float_t * f);
int  kz_getnumber(kz_endpoint_t * K, kz_float_t * f);
void kz_getreset(kz_endpoint_t * K);

/* place data in the put buffer */
int  kz_putint(kz_endpoint_t * K, kz_int_t i);
int  kz_putfloat(kz_endpoint_t * K, kz_float_t f);
int  kz_putstring(kz_endpoint_t * K, const kz_string_t * v);
int  kz_putlistopen(kz_endpoint_t * K);
int  kz_putlistclose(kz_endpoint_t * K);
int  kz_putnil(kz_endpoint_t * K);
/* clear put buffer */
void kz_putclear(kz_endpoint_t * K);

#endif
