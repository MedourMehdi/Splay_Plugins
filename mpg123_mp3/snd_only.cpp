/* .aac Atari player demo by MedMed and OL */

#include <stdio.h>
#include <string.h>
#include <sys/types.h>

#include <mint/osbind.h>
#include <mint/mintbind.h>
#include <mint/falcon.h>

#include <mpg123.h>

#ifndef cdecl
#define cdecl __CDECL
#endif

#ifndef SIGUSR1
#define SIGTERM 15
#define SIGUSR1 29
#endif				

typedef signed short SHORT;
typedef SHORT INT_PCM;

#define min(a,b) (a<=b?a:b)
#define max(a,b) (a>=b?a:b)

int16_t attenuation_left, attenuation_right;
int16_t loadNewSample;
int16_t clk_prescale;
int8_t* pPhysical;
int8_t* pLogical;

mpg123_handle* handle;

u_int32_t total_length;
double track_duration;
u_int32_t max_time_process, min_time_process;
u_int32_t time_start;
int16_t	clock_unit = 5;
u_int32_t time_end;
u_int32_t duration;
u_int32_t frames_counter;
bool is_destination_BE = true;
int sample_rate, channels, bits_per_sample, format;
int8_t* pBuffer;
u_int32_t bufferSize;
u_int32_t atari_hw_samplerate;

u_int32_t processedSize = 0;
bool processed_done = false;
const char* soundfile;

void st_MP3_Open();
void st_MP3_Close();
void st_MP3_Decode( int8_t* this_pBuffer );

int32_t get200hz(void);

void enableTimerASei( void );
void st_Snd_LoadBuffer( int8_t* );
void st_Snd_Feed();

short run=1, listen=1;
/* signal handler */
void cdecl sig_event(long id) 
{
	if(id==SIGUSR1)
	{
		if(listen) listen=0;
		else listen=1;
	}
	if(id==SIGTERM)
	{
		run=0;  /* exit program */
	}
}


void __attribute__((interrupt)) timerA( void )
{
	loadNewSample = 1;
	*( (volatile unsigned char*)0xFFFFFA0FL ) &= ~( 1<<5 );	//	clear in service bit
}


void enableTimerASei( void )
{
	*( (volatile unsigned char*)0xFFFFFA17L ) |= ( 1<<3 );	//	software end-of-interrupt mode
}

/****************************************************************/
/*            Sound Routines							        */
/****************************************************************/


int main(int argc, char *argv[])
{	short prev;
	long mask;

	soundfile = argv[1];
	if((soundfile == NULL)||(*soundfile==0)) return 0;
	
		
	mask=Psigblock(0);
#ifdef __PUREC__
	Psignal(SIGUSR1,   (__sig_handler *)sig_event);
	Psignal(SIGTERM,   (__sig_handler *)sig_event);
#else
	Psignal(SIGUSR1,   (long)sig_event);
	Psignal(SIGTERM,   (long)sig_event);
#endif
	Psigsetmask(mask&((~(1L<<SIGUSR1))&(~(1L<<SIGTERM))));

	st_MP3_Open();

	if( !processed_done ) 
	{
		if(Locksnd()==1) /* On signale au bios que la partie son de l'ordi est réservée */
		{
	    attenuation_left = (short)Soundcmd( LTATTEN, SND_INQUIRE ); /* recupère le niveau du volume gauche */
	    attenuation_right = (short)Soundcmd( RTATTEN, SND_INQUIRE ); /* volume droit */

	    clk_prescale = (((25175000 >> 8 ) / sample_rate - 1) ) ;
		if(clk_prescale == 1){
			atari_hw_samplerate = 49170;
		} else if(clk_prescale == 2){
			atari_hw_samplerate = 32780;
		} else if(clk_prescale == 3){
			atari_hw_samplerate = 24594;
	    } else if(clk_prescale == 4){
			atari_hw_samplerate = 19668;
	    } else if(clk_prescale == 0){
			atari_hw_samplerate = 49170;
			clk_prescale = 1;		
	    } else {
			printf("ERROR: Can not determine samplerate\n");
			return 1;
		}
		printf("\n###\tPlaying at %luHz\n", atari_hw_samplerate);
	    bufferSize = atari_hw_samplerate << 2;	// 2 channels * 16 bit * FREQ Hz * 1 second
	    pBuffer = (int8_t*)Mxalloc(bufferSize << 1, 0); /* Fois 2 parceque double buffer de son */
		// total_length = (bufferSize * track_duration) / 1000;
	    /* Pointeurs vers le double buffer son */
	    pPhysical = pBuffer;
 	    pLogical = pBuffer + bufferSize;

	    /* On rempli pPhysical avec la data PCM */
	    st_Snd_LoadBuffer( pPhysical );

	    /* Sndstatus() can be used to test the error condition of the sound system and to completely reset it */
	    Sndstatus( SND_RESET );

	    int32_t curadder = Soundcmd(ADDERIN, SND_INQUIRE); /* on recupère la source hardware actuelle */
	    int32_t curadc = Soundcmd(ADCINPUT, SND_INQUIRE);

	    Soundcmd(ADCINPUT, 0); /* On set la source harware */

    	Setmode( MODE_STEREO16 ); /* 16bit */

	    Devconnect( DMAPLAY, DAC, CLK25M, clk_prescale, NO_SHAKE ); /* conf. Hardaware */
	
	    Setbuffer( SR_PLAY, pPhysical, pPhysical + bufferSize ); /* On indique le début et la fin du buffer son */

	    Setinterrupt( SI_TIMERA, SI_PLAY );

	    Xbtimer( XB_TIMERA, 1<<3, 1, timerA );	// event count mode, count to '1'

	    Supexec(enableTimerASei); 

	    Jenabint( MFP_TIMERA );

	    Buffoper( SB_PLA_ENA | SB_PLA_RPT ); /* On joue le son */

	    loadNewSample = 1;
	    prev = 1;

	    while( (processedSize < total_length) && run ){
			if(listen) 
			{
				if(!prev)
				{
					Setbuffer( SR_PLAY, pPhysical, pPhysical + bufferSize );
					Jenabint( MFP_TIMERA );
					Buffoper( SB_PLA_ENA | SB_PLA_RPT ); /* On joue le son */
					prev=1;
					loadNewSample = 1;
				}
				st_Snd_Feed();
			}
			else
			{
				if(prev)
				{
					Buffoper( 0x00 );	// disable playback
					Jdisint( MFP_TIMERA );
					prev=0;
				}
				(void)Fselect(300L,0L,0L,0L); /* pause 300ms */ 
			}
	    }
	    if(prev)
	    {
	    	Buffoper( 0x00 );	// disable playback
	    	Jdisint( MFP_TIMERA );
	    }
	    Soundcmd( LTATTEN, attenuation_left );
	    Soundcmd( RTATTEN, attenuation_right );
	    Unlocksnd(); 
	  }
	}

	printf("\n###\tBest time to process 1000ms of sound => %lums\n###\tWorst time to process 1000ms of sound => %lums\n", min_time_process, max_time_process);

	st_MP3_Close();
	Mfree( pBuffer );
	pBuffer = NULL;

	return 0;
}

void st_Snd_LoadBuffer( int8_t* sndBuffer ){
	time_start = Supexec(get200hz);
	st_MP3_Decode( sndBuffer );
	processedSize += bufferSize;
	time_end = Supexec(get200hz);
	duration = (time_end - time_start) * clock_unit;
	max_time_process = max(max_time_process, duration);
	min_time_process = min_time_process != 0 ? min(min_time_process, duration) : duration;
}

void st_Snd_Feed()
{
	if( loadNewSample )
	{
		/* fill in logical buffer */
		st_Snd_LoadBuffer( pLogical );

		/* swap buffers (makes logical buffer physical) */
		int8_t* tmp = pPhysical;
		pPhysical = pLogical;
		pLogical = tmp;

		/* set physical buffer for the next frame */
		Setbuffer( SR_PLAY, pPhysical, pPhysical + bufferSize );

		loadNewSample = 0;
	}
	else Syield();
}

void st_MP3_Open(){

	mpg123_init();

    handle = mpg123_new(NULL, NULL);
    mpg123_open(handle, soundfile);

	long this_samplerate = 49170;
	int this_channels = 2;
	int encoding;

	double this_duration;
	
	// mpg123_getformat(handle, &this_samplerate, &this_channels, &encoding);
	mpg123_param( handle, MPG123_FORCE_RATE, this_samplerate, 0 );

	mpg123_param( handle, MPG123_FLAGS, MPG123_FORCE_STEREO | MPG123_QUIET, 0 ) ;

	mpg123_format_none( handle ) ;

	mpg123_format( handle, this_samplerate, MPG123_STEREO, MPG123_ENC_SIGNED_16 );

	mpg123_scan(handle);

	off_t length = mpg123_length(handle);

	if (length == MPG123_ERR || length < 0)
		track_duration = 0;
	else
		track_duration = ( 1000 * ( (double) length / (double) this_samplerate) );

	total_length = length << 2;

    sample_rate = this_samplerate;
	channels = this_channels;
	processedSize = 0;

	clk_prescale = (((25175000 >> 8) / sample_rate) - 1) ;

	printf( "\n###\tUsing MPG123\n");
	printf( "\n###\tOriginal Sample Rate %dHz\n###\tTotal duration of track: %fs\n###\tPrescale: %d\n", sample_rate, track_duration/1000, clk_prescale );
}

void st_MP3_Decode( int8_t* this_pBuffer ){

	u_int32_t done = 0;
	int ret = MPG123_OK;
	size_t current_done = 0;

    while( done < bufferSize && ret == MPG123_OK) {

		size_t packet_size = mpg123_outblock(handle);
		unsigned char* pData = new unsigned char[packet_size];

		ret = mpg123_read( handle, (unsigned char*)pData, packet_size, &current_done ) ;

		u_int16_t* this_ptr = (u_int16_t*)&this_pBuffer[done];
		for (int16_t i = 0; i < current_done >> 1; i++) {
			*this_ptr++ = (( ((INT_PCM*) pData)[i] ) &0xFF00) | ((INT_PCM*) pData)[i] & 0x00FF;
		}
		done += current_done;
    }

}

void st_MP3_Close(){
    mpg123_close(handle);
    mpg123_delete(handle);
    mpg123_exit();
	processedSize = 0;
	Mfree(pBuffer);
}

int32_t get200hz(void)
{
	return *((int32_t*)0x4ba);
}
