
#include "kinzhal.c"

#include <check.h>
#include <stdlib.h>
#include <assert.h>

int  null_rx(kz_byte_t * byte) { return 0; }
void null_tx(const kz_byte_t * bytes, size_t size) {}

typedef struct test_endpoint {
  kz_endpointdef_t def;
  kz_endpoint_t endpoint;
} test_endpoint_t;

kz_endpoint_t * test_endpoint_init(test_endpoint_t * endpoint, size_t rx_space, size_t tx_space) {
  assert(rx_space);
  assert(tx_space);

  endpoint->def.rx_buffer      = malloc(rx_space);
  endpoint->def.rx_buffer_size = rx_space;

  endpoint->def.tx_buffer      = malloc(tx_space);
  endpoint->def.tx_buffer_size = tx_space;

  endpoint->def.rx = null_rx;
  endpoint->def.tx = null_tx;

  kz_init_static(&endpoint->endpoint, &endpoint->def);

  return &endpoint->endpoint;
}
void test_endpoint_deinit(test_endpoint_t * endpoint) {
  free(endpoint->def.rx_buffer);
  free(endpoint->def.tx_buffer);

  memset(endpoint, 0, sizeof(*endpoint));
}


void check_decode_packet(kz_endpoint_t * K,
                         const uint8_t * bytes_in,
                         size_t bytes_in_num, 
                         const uint8_t * bytes_out, 
                         size_t bytes_out_num) {
  size_t i;

  for(i = 0 ; i < bytes_in_num ; i ++) {
    if(i == bytes_in_num - 1) {
      ck_assert_int_eq(rx_decode(K, bytes_in[i]), 1);
    } else {
      ck_assert_int_eq(rx_decode(K, bytes_in[i]), 0);
    }
  }

  ck_assert_ptr_eq(K->rx_buffer_pos, K->rx_buffer + bytes_out_num);
  ck_assert_mem_eq(K->rx_buffer, bytes_out, bytes_out_num);
}


void loopback(kz_endpoint_t * K) {
  size_t len;
  kz_byte_t * tx_frame;
  kz_byte_t * rx_frame;

  tx_frame = K->tx_buffer + KZ_TX_HEADER_START;
  rx_frame = K->rx_buffer + KZ_RX_HEADER_START;

  len = K->putptr - tx_frame;

  ck_assert_uint_ne(len, 0);

  memcpy(rx_frame, tx_frame, len);

  K->getptr = K->rx_buffer + KZ_RX_PAYLOAD_START;
  K->rx_buffer_pos = rx_frame + len;
}


/*
 * Test template:
 *
START_TEST(...) {
  test_endpoint_t test_endpoint;
  kz_endpoint_t * K;

  K = test_endpoint_init(&test_endpoint, KZ_MAX_BUFFER_SIZE, KZ_MAX_BUFFER_SIZE);

  test_endpoint_deinit(&test_endpoint);
}
END_TEST
*/

START_TEST(encode_all_zeros) {
  int i, len;

  kz_byte_t * tx_data;
  test_endpoint_t test_endpoint;
  kz_endpoint_t * K;

  K = test_endpoint_init(&test_endpoint, KZ_MAX_BUFFER_SIZE, KZ_MAX_BUFFER_SIZE);

  tx_data = K->tx_buffer + KZ_TX_HEADER_START;


  /* test a zero-filled buffer at various lengths */
  for(len = 0 ; len < TX_MTU ; len ++) {
    memset(tx_data, 0, len);
    K->putptr = tx_data + len;

    tx_encode_and_send(K);

    /* put pointer should be reset */
    ck_assert_ptr_eq(K->putptr, K->tx_buffer + KZ_TX_PAYLOAD_START);

    /* a buffer of zeros should encode to all 1s followed by a 0 */
    ck_assert_uint_eq(K->tx_buffer[0], 1);

    for(i = 0 ; i < len ; i ++) {
      ck_assert_uint_eq(tx_data[i], 1);
    }
    /* last byte (past-end data byte) should always be zero */
    ck_assert_uint_eq(tx_data[len], 0);
  }

  test_endpoint_deinit(&test_endpoint);
}
END_TEST

START_TEST(encode_single_zero) {
  int i, j;

  kz_byte_t * tx_data;
  test_endpoint_t test_endpoint;
  kz_endpoint_t * K;

  K = test_endpoint_init(&test_endpoint, KZ_MAX_BUFFER_SIZE, KZ_MAX_BUFFER_SIZE);

  tx_data = K->tx_buffer + KZ_TX_HEADER_START;

  /* test a single zero in various places */
  for(i = 0 ; i < TX_MTU ; i ++) {
    /* set data region to all 1s */
    memset(tx_data, 1, TX_MTU);
    /* set the ith bit in the data region to 0 */
    tx_data[i] = 0;

    K->putptr = tx_data + TX_MTU;

    tx_encode_and_send(K);

    /* put pointer should be reset */
    ck_assert_ptr_eq(K->putptr, K->tx_buffer + KZ_TX_PAYLOAD_START);

    /* first byte should "point" to the location of the zero */
    ck_assert_uint_eq(K->tx_buffer[0], i + KZ_TX_HEADER_START);
    /* ith byte should "point" to the end */
    ck_assert_uint_eq(tx_data[i], TX_MTU - i);
    /* last byte (past-end data byte) should always be zero */
    ck_assert_uint_eq(tx_data[TX_MTU], 0);

    /* data bytes on [0, i) should still be 1 */
    for(j = 0 ; j < i ; j ++) {
      ck_assert_uint_eq(tx_data[j], 1);
    }

    /* bytes on (i, TX_MTU) should still be 1 */
    for(j = i + 1 ; j < TX_MTU ; j ++) {
      ck_assert_uint_eq(tx_data[j], 1);
    }
  }

  test_endpoint_deinit(&test_endpoint);
}
END_TEST

START_TEST(decode_empty_packet) {
  int i;

  test_endpoint_t test_endpoint;
  kz_endpoint_t * K;

  K = test_endpoint_init(&test_endpoint, KZ_MAX_BUFFER_SIZE, KZ_MAX_BUFFER_SIZE);

  /* zeros shouldn't trigger the decoder */
  for(i = 0 ; i < 100 ; i ++) {
    ck_assert_int_eq(rx_decode(K, 0x00), 0);
  }

  /* verify empty packet triggers decoder */
  ck_assert_int_eq(rx_decode(K, 0x01), 0);
  ck_assert_int_eq(rx_decode(K, 0x00), 1);

  /* verify rx is empty */
  ck_assert_ptr_eq(K->rx_buffer_pos, K->rx_buffer);

  test_endpoint_deinit(&test_endpoint);
}
END_TEST

START_TEST(decode_all_zeros) {
  int i, len;

  test_endpoint_t test_endpoint;
  kz_endpoint_t * K;

  K = test_endpoint_init(&test_endpoint, KZ_MAX_BUFFER_SIZE, KZ_MAX_BUFFER_SIZE);

  /* for all possible lengths, spoof reception of a encoded packet containing zeros */
  for(len = 1 ; len < TX_MTU ; len ++) {
    /* start byte */
    ck_assert_int_eq(rx_decode(K, 0x01), 0);
    for(i = 0 ; i < len ; i ++) {
      ck_assert_int_eq(rx_decode(K, 0x01), 0);
    }
    /* stop byte */
    ck_assert_int_eq(rx_decode(K, 0x00), 1);

    /* `len` bytes should have been received by now */
    ck_assert_ptr_eq(K->rx_buffer_pos, K->rx_buffer + len);

    /* all bytes received should be zero */
    for(i = 0 ; i < len ; i ++) {
      ck_assert_uint_eq(K->rx_buffer[i], 0);
    }
  }

  test_endpoint_deinit(&test_endpoint);
}
END_TEST

START_TEST(decode_no_zeros) {
  int len, i;

  test_endpoint_t test_endpoint;
  kz_endpoint_t * K;

  K = test_endpoint_init(&test_endpoint, KZ_MAX_BUFFER_SIZE, KZ_MAX_BUFFER_SIZE);

  /* for all possible lengths, spoof reception of a encoded packet containing no zeros */
  for(len = 1 ; len < TX_MTU ; len ++) {
    /* start byte */
    ck_assert_int_eq(rx_decode(K, len + 1), 0);
    for(i = 0 ; i < len ; i ++) {
      ck_assert_int_eq(rx_decode(K, i + 1), 0);
    }
    /* stop byte */
    ck_assert_int_eq(rx_decode(K, 0x00), 1);

    /* `len` bytes should have been received by now */
    ck_assert_ptr_eq(K->rx_buffer_pos, K->rx_buffer + len);

    /* all bytes received should match */
    for(i = 0 ; i < len ; i ++) {
      ck_assert_uint_eq(K->rx_buffer[i], i + 1);
    }
  }

  test_endpoint_deinit(&test_endpoint);
}
END_TEST

START_TEST(decode_various) {
  const uint8_t packet0_in[] = { 0x01, 0x01, 0x00 };
  const uint8_t packet0_out[] = { 0x00 };
  const uint8_t packet1_in[] = { 0x01, 0x01, 0x01, 0x00 };
  const uint8_t packet1_out[] = { 0x00, 0x00 };
  const uint8_t packet2_in[] = { 0x04, 0xFF, 0xFE, 0xFD, 0x00 };
  const uint8_t packet2_out[] = { 0xFF, 0xFE, 0xFD };
  const uint8_t packet3_in[] = { 0x04, 0xFF, 0xFE, 0xFD, 0x03, 0xFB, 0xFA, 0x00 };
  const uint8_t packet3_out[] = { 0xFF, 0xFE, 0xFD, 0x00, 0xFB, 0xFA };

  test_endpoint_t test_endpoint;
  kz_endpoint_t * K;

  K = test_endpoint_init(&test_endpoint, KZ_MAX_BUFFER_SIZE, KZ_MAX_BUFFER_SIZE);

  /* verify packets */
  check_decode_packet(K, packet0_in, sizeof(packet0_in), packet0_out, sizeof(packet0_out));
  check_decode_packet(K, packet1_in, sizeof(packet1_in), packet1_out, sizeof(packet1_out));
  check_decode_packet(K, packet2_in, sizeof(packet2_in), packet2_out, sizeof(packet2_out));
  check_decode_packet(K, packet3_in, sizeof(packet3_in), packet3_out, sizeof(packet3_out));

  test_endpoint_deinit(&test_endpoint);
}
END_TEST

START_TEST(decode_overrun) {
  int i, len;

  test_endpoint_t test_endpoint;
  kz_endpoint_t * K;

  /* test for various sizes of rx_buffer */
  for(len = KZ_MIN_BUFFER_SIZE ; len < KZ_MAX_BUFFER_SIZE ; len += 5) {
    K = test_endpoint_init(&test_endpoint, len, KZ_MAX_BUFFER_SIZE);

    /* try to send a packet containing exactly what can fit */

    /* start byte */
    ck_assert_int_eq(rx_decode(K, 0x01), 0);
    /* zero bytes */
    for(i = 0 ; i < len ; i ++) {
      ck_assert_int_eq(rx_decode(K, 0x01), 0);
    }
    /* stop byte */
    ck_assert_int_eq(rx_decode(K, 0x00), 1);

    ck_assert_ptr_eq(K->rx_buffer_pos, K->rx_buffer + len);

    /* try to send a packet containing exactly one more than what can fit */

    /* start byte */
    ck_assert_int_eq(rx_decode(K, 0x01), 0);
    /* zero bytes */
    for(i = 0 ; i < len + 1 ; i ++) {
      ck_assert_int_eq(rx_decode(K, 0x01), 0);
    }
    /* stop byte, should not trigger decoder */
    ck_assert_int_eq(rx_decode(K, 0x00), 0);

    /* try to send a packet containing way more than what can fit */

    /* start byte */
    ck_assert_int_eq(rx_decode(K, 0x01), 0);
    /* zero bytes */
    for(i = 0 ; i < len*53 ; i ++) {
      ck_assert_int_eq(rx_decode(K, 0x01), 0);
    }
    /* stop byte, should not trigger decoder */
    ck_assert_int_eq(rx_decode(K, 0x00), 0);

    test_endpoint_deinit(&test_endpoint);
  }
}
END_TEST

START_TEST(putget_ints) {
  int i, j;

  test_endpoint_t test_endpoint;
  kz_endpoint_t * K;

  K = test_endpoint_init(&test_endpoint, KZ_MAX_BUFFER_SIZE, KZ_MAX_BUFFER_SIZE);

  for(i = 0 ; i < 1000 ; i ++) {
    kz_int_t integer_in;
    kz_int_t integer_out;
    int      range_select;

    for(j = 0 ; j < sizeof(integer_in) ; j ++) {
      ((uint8_t *)&integer_in)[j] = rand();
    }

    range_select = rand() % 4;

    if(range_select == 0) { integer_in = integer_in % INT8_MAX; }
    if(range_select == 1) { integer_in = integer_in % INT16_MAX; }
    if(range_select == 2) { integer_in = integer_in % INT32_MAX; }
    if(range_select == 3) { integer_in = integer_in % INT64_MAX; }

    kz_putclear(K);
    kz_putint(K, integer_in);

    loopback(K);

    ck_assert_int_eq(kz_getint(K, &integer_out), 1);
    ck_assert_int_eq(integer_in, integer_out);
  }

  test_endpoint_deinit(&test_endpoint);
}
END_TEST

START_TEST(putget_floats) {
  int i;

  test_endpoint_t test_endpoint;
  kz_endpoint_t * K;

  K = test_endpoint_init(&test_endpoint, KZ_MAX_BUFFER_SIZE, KZ_MAX_BUFFER_SIZE);

  for(i = 0 ; i < 1000 ; i ++) {
    int j;

    kz_float_t float_in;
    kz_float_t float_out;

    for(j = 0 ; j < sizeof(float_in) ; j ++) {
      ((uint8_t *)&float_in)[j] = rand();
    }

    kz_putclear(K);
    kz_putfloat(K, float_in);

    loopback(K);

    ck_assert_int_eq(kz_getfloat(K, &float_out), 1);
    ck_assert_float_eq(float_in, float_out);
  }

  test_endpoint_deinit(&test_endpoint);
}
END_TEST

/*
START_TEST(putget_misc) {
  test_endpoint_t test_endpoint;
  kz_endpoint_t * K;

  K = test_endpoint_init(&test_endpoint, KZ_MAX_BUFFER_SIZE, KZ_MAX_BUFFER_SIZE);

  test_endpoint_deinit(&test_endpoint);
}
END_TEST

START_TEST(putget_overrun) {
  test_endpoint_t test_endpoint;
  kz_endpoint_t * K;

  K = test_endpoint_init(&test_endpoint, KZ_MAX_BUFFER_SIZE, KZ_MAX_BUFFER_SIZE);

  test_endpoint_deinit(&test_endpoint);
}
END_TEST 
*/

Suite * kazhal_suite(void) {
  Suite * s;
  TCase * tc_core;

  s = suite_create("kazhal");

  tc_core = tcase_create("Core");

  tcase_add_test(tc_core, encode_all_zeros);
  tcase_add_test(tc_core, encode_single_zero);

  tcase_add_test(tc_core, decode_empty_packet);
  tcase_add_test(tc_core, decode_all_zeros);
  tcase_add_test(tc_core, decode_no_zeros);
  tcase_add_test(tc_core, decode_various);
  tcase_add_test(tc_core, decode_overrun);

  tcase_add_test(tc_core, putget_ints);
  tcase_add_test(tc_core, putget_floats);
  /*
  tcase_add_test(tc_core, putget_misc);
  tcase_add_test(tc_core, putget_overrun);
  */

  suite_add_tcase(s, tc_core);

  return s;
}

int main(void) {
  int number_failed;
  Suite * s;
  SRunner * sr;

  s = kazhal_suite();
  sr = srunner_create(s);

  srunner_run_all(sr, CK_VERBOSE);
  number_failed = srunner_ntests_failed(sr);

  srunner_free(sr);

  return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}

