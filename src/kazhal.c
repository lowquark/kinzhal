
#include "kazhal.h"

#include <string.h>

#define TX_MTU 254

#define KZ_HEADER_REQUEST   0x50
#define KZ_HEADER_REPLY     0x51

#define KZ_HEADER_SIZE         4

#define KZ_BC_NIL       0x80
#define KZ_BC_LISTOPEN  0x81
#define KZ_BC_LISTCLOSE 0x82

#define KZ_BC_FLOAT32   0x84
#define KZ_BC_FLOAT64   0x85

#define KZ_BC_INT8      0x88
#define KZ_BC_INT16     0x89
#define KZ_BC_INT32     0x8A
#define KZ_BC_INT64     0x8B

/* tx_buffer:
 *
 * 0            1      2      ...
 * v            v      v
 * [ reserved ] [ h0 ] [ h1 ] [ h2 ] [ h3 ] [ p0 ] [ p1 ] ... [ pN ] [ reserved ]
 *              ^                           ^
 *              |                           |
 *         HEADER_START               PAYLOAD_START
 *
 *
 * - Reserved bytes are for COBS encoding
 */

#define KZ_TX_HEADER_START      1
#define KZ_TX_PAYLOAD_START     (KZ_TX_HEADER_START + KZ_HEADER_SIZE)

#define KZ_RX_HEADER_START      0
#define KZ_RX_PAYLOAD_START     (KZ_RX_HEADER_START + KZ_HEADER_SIZE)

/* [ 0x50 ] [ REQID ] [  CHANID  ] [ reserved ]
 * [ 0x51 ] [ REQID ] [ reserved ] [ reserved ]
 * [ 0x52 ] [ REQID ] [ reserved ] [ reserved ]
 */

/* Decodes a byte into the endpoint's receive (RX) buffer.
 * Returns 1 if a frame has been finished, returns 0 otherwise.
 * If an invalid COBS sequence is found (unexpected zeros), waits for the start of the next frame.
 * If the end of the RX buffer is reached, waits for the end of the current frame and does not return 1.
 */
static char rx_decode(kz_endpoint_t * K, kz_byte_t byte) {
  switch(K->rx_state) {
    case KZ_RX_IDLE:
      if(byte != 0) {
        K->rx_state = KZ_RX_DECODE;
        K->rx_count = byte - 1;
        K->rx_buffer_pos = K->rx_buffer;
      }
      return 0;

    case KZ_RX_ABORT:
      if(byte == 0) {
        K->rx_state = KZ_RX_IDLE;
      }
      return 0;

    case KZ_RX_DECODE:
      if(byte == 0) {
        /* end of frame found */
        K->rx_state = KZ_RX_IDLE;
        /* if rx_count is zero, it was to be expected */
        return K->rx_count == 0;
      } else {
        if(K->rx_buffer_pos >= K->rx_buffer_end) {
          K->rx_state = KZ_RX_ABORT;
        } else {
          if(K->rx_count == 0) {
            /* just an ordinary header */
            *K->rx_buffer_pos++ = 0x00;
            K->rx_count = byte;
          } else {
            /* just an ordinary byte */
            *K->rx_buffer_pos++ = byte;
          }
          K->rx_count --;
        }
        return 0;
      }

    default:
      return 0;
  }
}

/* Encodes the transmit (TX) buffer in-place, and sends the resulting string via the tx handler
 * In order to encode in-place, the first and last bytes of the tx_buffer are reserved for byte stuffing.
 *
 * [ reserved ] [ d1 ] [ d2 ] [ d3 ] ... [ dN ] [ reserved ]
 *
 * It is not possible to perform in-place COBS encoding of frames larger than 254 bytes.
 */
static void tx_encode_and_send(kz_endpoint_t * K) {
  const kz_byte_t * data_end;
  kz_byte_t * search_ptr;
  kz_byte_t * len_ptr;
  uint_fast8_t len;

  /* TX function must be initialized */
  KZ_ASSERT(K->tx);
  /* The frame's data is assumed to be present in [K->tx_buffer + 1, K->putptr)
   * The # of bytes in this range must be less than the MTU
   */
  KZ_ASSERT(K->putptr - (K->tx_buffer + 1) <= TX_MTU);
  /* Check to make sure `K->putptr` is within bounds
   * (`*K->putptr` must be valid)
   */
  KZ_ASSERT(K->putptr <= K->tx_buffer_end - 1);

  /* Searches for the next zero byte contained within the data */
  search_ptr = K->tx_buffer + KZ_TX_HEADER_START;
  /* Stores location of last COBS subheader (number of bytes until next zero byte) */
  len_ptr = K->tx_buffer;

  /* Past-end pointer of TX data */
  data_end = K->putptr;

  len = 1;

  while(search_ptr != data_end) {
    if(*search_ptr == 0x00) {
      *len_ptr = len;
      len_ptr = search_ptr;
      len = 1;
    } else {
      len ++;
    }
    search_ptr ++;
  }

  *len_ptr = len;
  *search_ptr = 0x00;
  search_ptr ++;

  /* send all bytes in the newly encoded buffer */
  K->tx(K->tx_buffer, search_ptr - K->tx_buffer);
  /* reset write pointer */
  K->putptr = K->tx_buffer + KZ_TX_PAYLOAD_START;
}

static void send_reply(kz_endpoint_t * K, kz_byte_t reqid, kz_request_status_t stat) {
  /* these bytes are reserved for the header */
  K->tx_buffer[1] = KZ_HEADER_REPLY;
  K->tx_buffer[2] = reqid;
  K->tx_buffer[3] = 0x00;
  K->tx_buffer[4] = 0x00;

  tx_encode_and_send(K);
}

static void send_request(kz_endpoint_t * K, kz_byte_t reqid, kz_byte_t channelid) {
  /* these bytes are reserved for the header */
  K->tx_buffer[1] = KZ_HEADER_REQUEST;
  K->tx_buffer[2] = reqid;
  K->tx_buffer[3] = channelid;
  K->tx_buffer[4] = 0x00;

  tx_encode_and_send(K);
}

static void handle_request(kz_endpoint_t * K, unsigned int reqid, unsigned int channelid) {
  const unsigned int max_channels = sizeof(K->handlers)/sizeof(K->handlers[0]);

  kz_request_handler_t  handler;
  kz_request_status_t   status;

  if(channelid < max_channels) {
    /* find a associated handler for this request */
    handler = K->handlers[channelid];

    if(handler.callback) {
      /* get ready to read */
      K->getptr = K->rx_buffer + KZ_RX_PAYLOAD_START;

      /* call the handler */
      status = handler.callback(K, handler.userdata);

      if(status != KZ_IGNORE) {
        send_reply(K, reqid, status);
      }
    }
  }

  /* ignore this request, its channelid has no handler */
}

static void handle_reply(kz_endpoint_t * K, unsigned int reqid) {
  kz_local_request_t * req;

  /* reqid happens to index directly into local_requests */
  const unsigned int max_local_requests = sizeof(K->local_requests)/sizeof(K->local_requests[0]);

  /* verify index is accessible */
  if(reqid < max_local_requests) {
    /* find associated request */
    req = K->local_requests + reqid;
    /* check to see if this reqid is active */
    if(req->callback) {
      /* get ready to read */
      K->getptr = K->rx_buffer + KZ_RX_PAYLOAD_START;

      /* active, call its handler */
      /* TODO: pass in status */
      req->callback(K, req->userdata, KZ_OK);

      req->callback      = NULL;
      req->userdata      = NULL;
      req->timeout_ticks = 0;
    }
  }
}

static void handle_timeouts(kz_endpoint_t * K) {
  const unsigned int max_local_requests = sizeof(K->local_requests)/sizeof(K->local_requests[0]);

  kz_local_request_t * req;
  kz_local_request_t * local_requests_end;

  local_requests_end = K->local_requests + max_local_requests;

  /* decrement timeout for all local requests */
  for(req = K->local_requests ;
      req != local_requests_end ;
      req ++) {
    if(req->callback) {
      /* this request is active */
      req->timeout_ticks --;

      if(req->timeout_ticks <= 0) {
        /* get ready to read nothing */
        K->rx_buffer_pos = K->rx_buffer + KZ_RX_PAYLOAD_START;
        K->getptr = K->rx_buffer_pos;

        /* timed out, give it the ignore signal */
        req->callback(K, req->userdata, KZ_IGNORE);

        req->callback      = NULL;
        req->userdata      = NULL;
        req->timeout_ticks = 0;
      }
    }
  }
}

void kz_init_static(kz_endpoint_t * K, const kz_endpointdef_t * def) {
  /* Initialize RX buffer */
  K->rx_buffer     = def->rx_buffer;
  K->rx_buffer_pos = def->rx_buffer;
  K->rx_buffer_end = def->rx_buffer + def->rx_buffer_size;

  /* Initialize TX buffer */
  K->tx_buffer     = def->tx_buffer;
  K->tx_buffer_end = def->tx_buffer + def->tx_buffer_size;

  K->getptr = def->rx_buffer + KZ_RX_PAYLOAD_START; /* initialize to beginning of payload */
  K->putptr = def->tx_buffer + KZ_TX_PAYLOAD_START; /* initialize to beginning of payload */

  /* Initialize serial rx/tx handlers */
  K->rx = def->rx;
  K->tx = def->tx;

  /* Initialize list of request handlers */
  memset(K->handlers, 0, sizeof(K->handlers));

  /* Initialize pool of local request objects */
  memset(K->local_requests, 0, sizeof(K->local_requests));

  K->rx_state = KZ_RX_IDLE;
  K->rx_count = 0;
}


int kz_handle(kz_endpoint_t * K, unsigned int channelid, kz_request_handler_fn_t callback, void * userdata) {
  const unsigned int max_channels = sizeof(K->handlers)/sizeof(K->handlers[0]);

  if(channelid < max_channels) {
    K->handlers[channelid].callback = callback;
    K->handlers[channelid].userdata = userdata;
    return 1;
  } else {
    return 0;
  }
}

int kz_call(kz_endpoint_t * K, unsigned int channelid, kz_reply_handler_fn_t callback, void * userdata, int timeout_ticks) {
  const unsigned int max_local_requests = sizeof(K->local_requests)/sizeof(K->local_requests[0]);

  kz_local_request_t * req;
  kz_local_request_t * local_requests_end;
  kz_byte_t reqid = 0;

  local_requests_end = K->local_requests + max_local_requests;

  /* find unused local request object in pool */
  for(req = K->local_requests ;
      req != local_requests_end ;
      req ++) {
    /* check handler field to determine whether this object is in use */
    if(!req->callback) {
      /* found unused object, allocate for this outgoing request */
      req->callback      = callback;
      req->userdata      = userdata;
      req->timeout_ticks = timeout_ticks;

      /* actually send data */
      send_request(K, reqid, channelid);

      return 1;
    }

    reqid ++;
  }

  return 0;
}

void kz_send(kz_endpoint_t * K, unsigned int channelid) {
  /* just send data */
  send_request(K, 0xFF, channelid);
}

void kz_tick(kz_endpoint_t * K) {
  kz_byte_t reqid;
  kz_byte_t channelid;
  kz_byte_t byte;

  /* call rx until it indicates no more bytes to be received */
  while(K->rx(&byte)) {
    /* decode this byte as part of the in-progress rx frame */
    if(rx_decode(K, byte)) {
      /* frame received! */
      size_t size = K->rx_buffer_pos - K->rx_buffer;
      kz_byte_t * const frame = K->rx_buffer;

      if(size >= 4) {
        switch(frame[0]) {
          case KZ_HEADER_REQUEST:
            reqid = frame[1];
            channelid = frame[2];
            handle_request(K, reqid, channelid);
            break;

          case KZ_HEADER_REPLY:
            reqid = frame[1];
            handle_reply(K, reqid);
            break;

          default:
            break;
        }
      }
    }
  }

  /* call call handlers who have timed out */
  handle_timeouts(K);
}

int kz_getint(kz_endpoint_t * K, kz_int_t * i) {
  union {
    int8_t i8;
    uint16_t u16;
    uint32_t u32;
    uint64_t u64;
  } s;

  const kz_byte_t * const getend = K->rx_buffer_pos;

  kz_byte_t    header_byte;
  kz_byte_t *  getptr;
  uint_fast8_t secondary_size;

  getptr = K->getptr;

  if(getend - getptr < 1) {
    /* no bytes available to read */
    goto notanint;
  }

  header_byte = *getptr++;

  /* interpret header byte as a signed 8-bit int */
  s.i8 = header_byte;

  if(s.i8 >= -64 && s.i8 <= 127) {
    /* This is a sigle byte integer */
    *i = s.i8;
    secondary_size = 0;

    goto done;
  } else {
    /* Check other int types */
    switch(header_byte) {
      case KZ_BC_INT8:
        secondary_size = 1;
        break;

      case KZ_BC_INT16:
        if(sizeof(*i) >= 2) {
          secondary_size = 2;
          break;
        } else {
          /* type not supported */
          goto unsupported;
        }

      case KZ_BC_INT32:
        if(sizeof(*i) >= 4) {
          secondary_size = 4;
          break;
        } else {
          /* type not supported */
          goto unsupported;
        }

      case KZ_BC_INT64:
        if(sizeof(*i) >= 8) {
          secondary_size = 8;
          break;
        } else {
          /* type not supported */
          goto unsupported;
        }

      default:
        /* not an int */
        goto notanint;
    }

    if(getend - getptr < secondary_size) {
      /* not enough bytes to store this int type */
      goto notanint;
    }

    switch(header_byte) {
      case KZ_BC_INT8:
        *i = (int8_t)(getptr[0]);
        goto done;

      case KZ_BC_INT16:
        /* this should have already been checked above */
        KZ_ASSERT(sizeof(*i) >= 2);

        s.u16 = ((uint16_t)getptr[0] <<  8) |
                ((uint16_t)getptr[1]      );

        *i = (int16_t)s.u16;

        goto done;

      case KZ_BC_INT32:
        /* this should have already been checked above */
        KZ_ASSERT(sizeof(*i) >= 4);

        s.u32 = ((uint32_t)getptr[0] << 24) |
                ((uint32_t)getptr[1] << 16) |
                ((uint32_t)getptr[2] <<  8) |
                ((uint32_t)getptr[3]      );

        *i = (int32_t)s.u32;

        goto done;

      case KZ_BC_INT64:
        /* this should have already been checked above */
        KZ_ASSERT(sizeof(*i) >= 8);

        s.u64 = ((uint64_t)getptr[0] << 56) |
                ((uint64_t)getptr[1] << 48) |
                ((uint64_t)getptr[2] << 40) |
                ((uint64_t)getptr[3] << 32) |
                ((uint64_t)getptr[4] << 24) |
                ((uint64_t)getptr[5] << 16) |
                ((uint64_t)getptr[6] <<  8) |
                ((uint64_t)getptr[7]      );

        *i = (int64_t)s.u64;

        goto done;

      default:
        /* Not an int, though the last one should have caught this :/ */
        goto notanint;
    }
  }

notanint:
unsupported:
  return 0;

done:
  K->getptr = getptr + secondary_size;

  return 1;
}


int kz_getfloat(kz_endpoint_t * K, kz_float_t * f) {
  union {
    float  f;
    double d;
  } s;

  const kz_byte_t * const getend = K->rx_buffer_pos;

  kz_byte_t    header_byte;
  kz_byte_t *  getptr;
  uint_fast8_t secondary_size;

  getptr = K->getptr;

  if(getend - getptr < 1) {
    /* no bytes available to read */
    goto notafloat;
  }

  header_byte = *getptr++;

  switch(header_byte) {
    case KZ_BC_FLOAT32:
      if(sizeof(float) == 4 && sizeof(*f) >= 4) {
        secondary_size = 4;
        break;
      } else {
        /* type not supported */
        goto unsupported;
      }

    case KZ_BC_FLOAT64:
      if(sizeof(double) == 8 && sizeof(*f) >= 8) {
        secondary_size = 8;
        break;
      } else {
        /* type not supported */
        goto unsupported;
      }

    default:
      /* type not a float */
      goto notafloat;
  }

  if(getend - getptr < secondary_size) {
    /* not enough bytes to store this float type */
    goto notafloat;
  }

  switch(header_byte) {
    case KZ_BC_FLOAT32:
      /* this should have already been checked above */
      KZ_ASSERT(sizeof(float) == 4 && sizeof(*f) >= 4);

      /* deserialize */
      ((kz_byte_t *)&s.f)[3] = getptr[0];
      ((kz_byte_t *)&s.f)[2] = getptr[1];
      ((kz_byte_t *)&s.f)[1] = getptr[2];
      ((kz_byte_t *)&s.f)[0] = getptr[3];

      *f = s.f;

      goto done;

    case KZ_BC_FLOAT64:
      /* this should have already been checked above */
      KZ_ASSERT(sizeof(double) == 8 && sizeof(*f) >= 8);

      ((kz_byte_t *)&s.d)[7] = getptr[0];
      ((kz_byte_t *)&s.d)[6] = getptr[1];
      ((kz_byte_t *)&s.d)[5] = getptr[2];
      ((kz_byte_t *)&s.d)[4] = getptr[3];
      ((kz_byte_t *)&s.d)[3] = getptr[4];
      ((kz_byte_t *)&s.d)[2] = getptr[5];
      ((kz_byte_t *)&s.d)[1] = getptr[6];
      ((kz_byte_t *)&s.d)[0] = getptr[7];

      *f = s.d;

      goto done;

    default:
      /* shouldn't actually get here */
      goto notafloat;
  }

notafloat:
unsupported:
  return 0;

done:
  K->getptr = getptr + secondary_size;

  return 1;
}


int kz_getnumber(kz_endpoint_t * K, kz_float_t * f) {
  kz_int_t i;
  if(kz_getfloat(K, f)) {
    return 1;
  } else if(kz_getint(K, &i)) {
    *f = i;
    return 1;
  } else {
    return 0;
  }
}


void kz_getreset(kz_endpoint_t * K) {
  K->getptr = K->rx_buffer + KZ_RX_PAYLOAD_START; /* initialize to beginning of payload */
}


int kz_putint(kz_endpoint_t * K, kz_int_t v) {
  kz_byte_t * const putend = K->tx_buffer_end - 1;

  union {
    uint16_t u16;
    uint32_t u32;
    uint64_t u64;
  } s;

  if(v >= -64 && v <= 127) {
    if(putend - K->putptr < 1) { return 0; }

    *K->putptr++ = (kz_byte_t)v;
  } else if(v >= INT16_MIN && v <= INT16_MAX) {
    if(putend - K->putptr < 3) { return 0; }

    s.u16 = v;

    *K->putptr++ = KZ_BC_INT16;
    *K->putptr++ = (s.u16 >> 8) & 0xFF;
    *K->putptr++ = (s.u16     ) & 0xFF;
  } else if(v >= INT32_MIN && v <= INT32_MAX) {
    if(putend - K->putptr < 5) { return 0; }

    s.u32 = v;

    *K->putptr++ = KZ_BC_INT32;
    *K->putptr++ = (s.u32 >> 24) & 0xFF;
    *K->putptr++ = (s.u32 >> 16) & 0xFF;
    *K->putptr++ = (s.u32 >>  8) & 0xFF;
    *K->putptr++ = (s.u32      ) & 0xFF;
  } else if(v >= INT64_MIN && v <= INT64_MAX) {
    if(putend - K->putptr < 9) { return 0; }

    s.u64 = v;

    *K->putptr++ = KZ_BC_INT64;
    *K->putptr++ = (s.u64 >> 56) & 0xFF;
    *K->putptr++ = (s.u64 >> 48) & 0xFF;
    *K->putptr++ = (s.u64 >> 40) & 0xFF;
    *K->putptr++ = (s.u64 >> 32) & 0xFF;
    *K->putptr++ = (s.u64 >> 24) & 0xFF;
    *K->putptr++ = (s.u64 >> 16) & 0xFF;
    *K->putptr++ = (s.u64 >>  8) & 0xFF;
    *K->putptr++ = (s.u64      ) & 0xFF;
  }

  return 1;
}
int kz_putfloat(kz_endpoint_t * K, kz_float_t v) {
  kz_byte_t * const putend = K->tx_buffer_end - 1;

  if(putend - K->putptr < sizeof(kz_float_t) + 1) { return 0; }

  if(sizeof(kz_float_t) == 8) {
    *K->putptr++ = KZ_BC_FLOAT64;
    *K->putptr++ = ((kz_byte_t *)&v)[7];
    *K->putptr++ = ((kz_byte_t *)&v)[6];
    *K->putptr++ = ((kz_byte_t *)&v)[5];
    *K->putptr++ = ((kz_byte_t *)&v)[4];
  } else if(sizeof(kz_float_t) == 4) {
    *K->putptr++ = KZ_BC_FLOAT32;
  } else {
    return 0;
  }

  *K->putptr++ = ((kz_byte_t *)&v)[3];
  *K->putptr++ = ((kz_byte_t *)&v)[2];
  *K->putptr++ = ((kz_byte_t *)&v)[1];
  *K->putptr++ = ((kz_byte_t *)&v)[0];

  return 1;
}
int kz_putstring(kz_endpoint_t * K, const kz_string_t * v) {
  kz_byte_t * const putend = K->tx_buffer_end - 1;

  if(putend - K->putptr < 1) { return 0; }

  *K->putptr++ = KZ_BC_NIL;

  return 1;
}
int kz_putlistopen(kz_endpoint_t * K) {
  kz_byte_t * const putend = K->tx_buffer_end - 1;

  if(putend - K->putptr < 1) { return 0; }

  *K->putptr++ = KZ_BC_LISTOPEN;

  return 1;
}
int kz_putlistclose(kz_endpoint_t * K) {
  kz_byte_t * const putend = K->tx_buffer_end - 1;

  if(putend - K->putptr < 1) { return 0; }

  *K->putptr++ = KZ_BC_LISTCLOSE;

  return 1;
}
int kz_putnil(kz_endpoint_t * K) {
  kz_byte_t * const putend = K->tx_buffer_end - 1;

  if(putend - K->putptr < 1) { return 0; }

  *K->putptr++ = KZ_BC_NIL;

  return 1;
}
void kz_putclear(kz_endpoint_t * K) {
  K->putptr = K->tx_buffer + KZ_TX_PAYLOAD_START; /* initialize to beginning of payload */
}

