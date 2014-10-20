#include <avr/interrupt.h>
#include <util/delay.h>
#include <spi.h>

#define FLASH_LED_COMMAND 	0x01
#define POLL_CMD 			0x02
#define PTT_FFSK_CMD		0x03
#define PTT_VOICE_CMD		0x04
#define RX_FFSK_MUTE_CMD	0x05
#define RX_VOICE_MUTE_CMD 	0x06
#define TX_VOICE_MUTE_CMD	0x07

//#define OTHER_SELECT_PIN PB6
//#define SELECT_OTHER PORTB &= ~(1<<OTHER_SELECT_PIN)
//#define DESELECT_OTHER PORTB |= (1<<OTHER_SELECT_PIN)

#define FFSK_MUTE 	PORTC3
#define AUDIO_MUTE 	PORTC2
#define M_SIG		PORTC1
#define E_SIG		PORTC0


#define SIG_MSG		0x01
#define FFSK_MSG	0x02
#define HB_MSG		0x04

#define DEF_BUFSIZE 20
volatile unsigned char incoming[20];
volatile unsigned char txbuf[20];
volatile short int received=0;
volatile short int BUFSIZE;
volatile uint8_t poll_st;
volatile uint8_t msg_cnt, tx_cnt;

uint8_t ffsk_msg01[11] = { 10, 'f', 'f', 's', 'k', '_', '0', '1', 'm', 's', 'g' };

// flash led that's connected to pin PD7
void flash_led(int count, int port)
{
  //PORTC |= (1<<port);
  for (int i=0; i<count*2; i++) {
	if (port < 5)
		PORTB ^= (1<<port);
	else
		PORTD ^= (1<<port);
    _delay_ms(100);
  }
}

// send a SPI message to the other device - 3 bytes then go back into 
// slave mode
void send_message()
{
#if 0
  setup_spi(SPI_MODE_1, SPI_MSB, SPI_NO_INTERRUPT, SPI_MSTR_CLK8);
  if (SPCR & (1<<MSTR)) { // if we are still in master mode
    SELECT_OTHER; // tell other device to flash LED twice
    send_spi(FLASH_LED_COMMAND); send_spi(0x02); send_spi(0x00);
    DESELECT_OTHER;
  }
  setup_spi(SPI_MODE_1, SPI_MSB, SPI_INTERRUPT, SPI_SLAVE);
#endif
}

// called when the button pushed and pin INT0 goes from 1 to 0
#if 0
ISR(INT0_vect)
{
  //send_message();
  //_delay_ms(500); // 'debounce'
}
#endif
// parse the data received from the other device
// currently just knows about the FLASH_LED_COMMAND
void parse_message()
{
  uint8_t i;
  switch(incoming[0]) {
  case FLASH_LED_COMMAND:
	if (BUFSIZE >= 2) {
		flash_led(incoming[1], incoming[2]);
	} else {
		flash_led(incoming[1], SYS_DBG01_LED);	
	}
    break;
  case POLL_CMD:
	  msg_cnt = 11;
	  txbuf[0] = 10;
	  for(i = 1; i < msg_cnt; i++) {
		txbuf[i] = i;
		PORTD ^= (1<<SYS_DBG06_LED);
	  }
	  poll_st = 1;	 
	  tx_cnt = 0;
  break;
  case PTT_FFSK_CMD:
  break;
  case PTT_VOICE_CMD:
  break;
  case RX_FFSK_MUTE_CMD:
  break;
  case RX_VOICE_MUTE_CMD:
  break;
  case TX_VOICE_MUTE_CMD:
  break;
  
  default:
    flash_led(2,SYS_DBG06_LED);
  }
}

// called by the SPI system when there is data ready.
// Just store the incoming data in a buffer, when we receive a
// terminating byte (0x00) call parse_message to process the data received
ISR(SPI_STC_vect)
{
  SPCR &= ~(1 << SPIE);
  if ((PORTB & 1) == 0) {
	  //if (SPSR && (1 << WCOL)) 
	  #if 0
	  {	
		  if (poll_st <= 0) {
			  PORTB &= ~(1 << SYS_DBG04_LED); 	  
			  received_from_spi(received);
		  } else {
			  PORTB &= ~(1 << SYS_DBG01_LED);
			  
			  received_from_spi(txbuf[tx_cnt]);
			  
		  }
	  } else 
	  #endif
	  {
		  if (poll_st <= 0) {
			  PORTB ^= (1 << SYS_DBG04_LED); 
			  if (received == 0) {
				BUFSIZE = received_from_spi(received);
				if (BUFSIZE < DEF_BUFSIZE)
					received++;
			  } else {	
				
				incoming[received-1] = received_from_spi(received);
				
				if (received >= BUFSIZE || incoming[received-1] == 0xFF) {
					parse_message();
					received = 0;
				} else
					received++;
			  }
		  } else {
			  PORTB ^= (1 << SYS_DBG01_LED);
			  
			  (void) received_from_spi(txbuf[tx_cnt]);
			  
			  if (tx_cnt >= msg_cnt) {
				poll_st = 0;
				received = 0;
			  } else
				tx_cnt++;
		  }
	  }
  } else
	PORTD ^= (1<<SYS_DBG06_LED);
  SPCR |= (1 << SPIE);
  sei();
}
 
int main(void)
{
  // make sure other device is unselected (pin is HIGH) and setup spi
  //DESELECT_OTHER;
  //DDRB |= (1<<OTHER_SELECT_PIN);
  //MCUCSR ^= (MCUCSR & 0x80);
  BUFSIZE = DEF_BUFSIZE;
  DDRD = 0x03;
  PORTD = 0x0;
  DDRE = 0xFE;
  PORTE = 0x0;
  DDRC = 0x0E;
  PORTC = 0x0;
  DDRB = 0xF8;
  PORTB |= 0xF8;
  poll_st = 0;
  received = 0;
  
  setup_spi(SPI_MODE_3, SPI_MSB, SPI_INTERRUPT, (SPI_SLAVE));
  
  // raise interrupt when the button is pushed and INT0 pin goes 
  // from 1 to 0 (pin PD0 at AT90usbXXX, pin PD2 on ATmegaXXX, 
  // arduino pin 2)). The code in ISR(INT0_vect) above will be called
  //EICRB = (1<<ISC01) | (0<<ISC00);
  //EIMSK |= (1<<INT0);
  sei();

  // flash LED at start to indicate were ready
  //flash_led(2,TX_FFSK);
  //_delay_ms(100);
 for(;;) {
	//PORTD ^= (1<<SYS_DBG06_LED);
	if ((PORTB & 1) == 1) {
		PORTE ^= (1<<SYS_LED);
	} else {
		PORTB ^= (1 << SYS_DBG03_LED);
		PORTD ^= (1<<SYS_DBG07_LED);
	}
	//flash_led(2,TX_FFSK);
    _delay_ms(100);
  } // do nothing
  //MCUCSR ^= (MCUCSR & 0x80);
}
