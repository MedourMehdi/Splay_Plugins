/* .wav Atari player demo by MedMed and OL */

#include <stdio.h>
#include <string.h>
#include <sys/types.h>


#include <mint/osbind.h>
#include <mint/mintbind.h>
#include <mint/falcon.h>

#include "./wav_lib/wavreader.h"

#ifndef cdecl
#define cdecl __CDECL
#endif

#ifndef SIGUSR1
#define SIGTERM 15
#define SIGUSR1 29
#endif				
									


int16_t attenuation_left, attenuation_right;
int16_t loadNewSample;
int16_t clk_prescale;
int8_t* pPhysical;
int8_t* pLogical;
void* wav;
int sample_rate, channels, bits_per_sample, format;
int8_t* pBuffer;
u_int32_t bufferSize;
u_int32_t atari_hw_samplerate;

u_int32_t processedSize = 0;
u_int32_t data_length;
const char* wavfile;

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

	wavfile = argv[1];
	if((wavfile == NULL)||(*wavfile==0)) return 0;
	
		
	mask=Psigblock(0);
#ifdef __PUREC__
	Psignal(SIGUSR1,   (__sig_handler *)sig_event);
	Psignal(SIGTERM,   (__sig_handler *)sig_event);
#else
	Psignal(SIGUSR1,   (long)sig_event);
	Psignal(SIGTERM,   (long)sig_event);
#endif
	Psigsetmask(mask&((~(1L<<SIGUSR1))&(~(1L<<SIGTERM))));
	
	wav = wav_read_open(wavfile);
	if(wav==NULL) return 0;
	wav_get_header(wav, &format, &channels, &sample_rate, &bits_per_sample, &data_length);
	
	if( processedSize < data_length + bufferSize ) 
	{
    	  if(Locksnd()==1) /* On signale au bios que la partie son de l'ordi est réservée */
    	  {
	    attenuation_left = (short)Soundcmd( LTATTEN, SND_INQUIRE ); /* recupère le niveau du volume gauche */
	    attenuation_right = (short)Soundcmd( RTATTEN, SND_INQUIRE ); /* volume droit */

	    clk_prescale = (((25175000 / 256 ) / sample_rate - 1) ) ;

	    if(clk_prescale == 3){
		atari_hw_samplerate =  24594;
	    }

	    bufferSize = atari_hw_samplerate << 2;	// 2 channels * 16 bit * FREQ Hz * 1 second
	    pBuffer = NULL;
	    pBuffer = (int8_t*)Mxalloc(bufferSize << 1, 0); /* Fois 2 parceque double buffer de son */

	    /* Pointeurs vers le double buffer son */
	    pPhysical = pBuffer;
 	    pLogical = pBuffer + bufferSize;

	    /* On rempli les deux buffer avec la data PCM */
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
	    while( (processedSize < data_length + bufferSize) && run ){ 

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
	wav_read_close(wav);
	Mfree( pBuffer );
	pBuffer = NULL;

	return 0;
}

void st_Snd_LoadBuffer( int8_t* sndBuffer ){
	u_int16_t pcm_tmp;
	u_int32_t i;
	wav_read_data(wav, (unsigned char *)&sndBuffer[0], bufferSize);

	for(i = 0; i <  bufferSize; i += 2){
		pcm_tmp = 0;

		pcm_tmp = (sndBuffer[i + 1]) << 8 | ((u_int16_t)sndBuffer[i] >> 8);
		memcpy(&sndBuffer[i], &pcm_tmp, sizeof(u_int16_t));
	}
	processedSize +=  bufferSize;
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


