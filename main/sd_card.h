/**
 *
 */
#ifndef __DS_CARD_H__
#define __DS_CARD_H__

void sd_card_init(void);
int read_pcm_file(void);
void sd_card_create_file(void);
void sd_card_write_data(const uint8_t *data, uint32_t len, size_t *bytes_written);

#endif // __DS_CARD_H__