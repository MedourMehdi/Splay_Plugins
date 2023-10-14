/* .aac Atari player demo by MedMed and OL */

#include <stdio.h>
#include <string.h>
#include <sys/types.h>

#include <mint/osbind.h>
#include <mint/mintbind.h>
#include <mint/falcon.h>

#include <neaacdec.h>
#include <mp4v2/mp4v2.h>

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
MP4FileHandle* pMP4_Handler;
NeAACDecHandle * pAAC_Handler;
int16_t Snd_track_number;
uint32_t total_frames;
int16_t max_frame_size;
double track_duration;
uint32_t max_time_process, min_time_process;
u_int32_t time_start;
int16_t	clock_unit = 5;
u_int32_t time_end;
u_int32_t duration;
uint32_t frames_counter;
int16_t is_destination_BE = TRUE;
int sample_rate, channels, bits_per_sample, format;
int8_t* pBuffer;
u_int32_t bufferSize;
u_int32_t atari_hw_samplerate;

u_int32_t processedSize = 0;
bool processed_done = false;
const char* soundfile;

void st_ACC_Open();
void st_AAC_Close();
void st_AAC_Decode( int8_t* this_pBuffer );
u_int16_t getFirstAudioTrack(MP4FileHandle *reader);
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

	st_ACC_Open();

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
	    } else {
			printf("ERROR: Can not determine samplerate\n");
			return 1;
		}
		printf("\n###\tPlaying at %luHz", atari_hw_samplerate);
	    bufferSize = atari_hw_samplerate << 2;	// 2 channels * 16 bit * FREQ Hz * 1 second
	    pBuffer = (int8_t*)Mxalloc(bufferSize << 1, 0); /* Fois 2 parceque double buffer de son */

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
	    while( frames_counter < total_frames && run ){ 
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

	st_AAC_Close();
	Mfree( pBuffer );
	pBuffer = NULL;

	return 0;
}

void st_Snd_LoadBuffer( int8_t* sndBuffer ){
	time_start = Supexec(get200hz);
	st_AAC_Decode( sndBuffer );
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

void st_ACC_Open(){
	pMP4_Handler = (MP4FileHandle*)MP4Read(soundfile);
	pAAC_Handler = (NeAACDecHandle*)Mxalloc(sizeof(NeAACDecHandle), 3);
	*pAAC_Handler = faacDecOpen();

	unsigned char *escfg;
	unsigned int escfglen;
	unsigned long this_samplerate;
	unsigned char this_channels;

    Snd_track_number = getFirstAudioTrack(pMP4_Handler);
    total_frames = MP4GetTrackNumberOfSamples(pMP4_Handler, Snd_track_number);
    max_frame_size = MP4GetTrackMaxSampleSize(pMP4_Handler, Snd_track_number);

    MP4GetTrackESConfiguration(pMP4_Handler, Snd_track_number, &escfg, &escfglen);
	if (!escfg) {
		printf("---\tNo audio format information found\n");
	}
	
	printf("###\tUsing FAAD library\n\n");

	if (faacDecInit2(*pAAC_Handler, escfg, escfglen, &this_samplerate, &this_channels) < 0) {
		printf("---\tCould not initialise FAAD\n");
	}

	printf("Found MP4 audio at track %u, sample rate %u, %u channels\n", Snd_track_number, this_samplerate, this_channels);

    // uint32_t packet_size = max_frame_size; 
	// uint8_t  pData[packet_size], *ptr = pData;

    // MP4ReadSample(pMP4_Handler, Snd_track_number, 1, (uint8_t **) &ptr, &packet_size, NULL, NULL, NULL, NULL);

	if (this_channels != 2) {
		printf("---\tBad number of channels\n");
	}

	processedSize = 0;

	sample_rate = MP4GetTrackTimeScale(pMP4_Handler, Snd_track_number);
	track_duration = double(MP4ConvertFromTrackDuration(pMP4_Handler, Snd_track_number, MP4GetTrackDuration(pMP4_Handler, Snd_track_number), MP4_MSECS_TIME_SCALE));
	clk_prescale = (((25175000 >> 8) / sample_rate) - 1) ;

	frames_counter = 1;
	printf( "\n###\tOriginal Sample Rate %dHz\n###\tTotal duration of track: %fs\n###\tPrescale: %d\n", sample_rate, track_duration/1000, clk_prescale );
}

void st_AAC_Decode( int8_t* this_pBuffer ){

    int16_t  frame_size = 0;
	u_int32_t done = 0;
    void* pDec_data;

    while( done < bufferSize && frames_counter < total_frames) {

		uint32_t    packet_size = max_frame_size; 
		u_int8_t  	pData[packet_size], *ptr = pData;
		NeAACDecFrameInfo snd_info;

		MP4ReadSample(pMP4_Handler, Snd_track_number, frames_counter, (uint8_t **) &ptr, &packet_size, NULL, NULL, NULL, NULL);

		pDec_data = faacDecDecode(*pAAC_Handler, &snd_info , ptr, packet_size);

        frame_size = snd_info.samples;

		u_int16_t* this_ptr = (u_int16_t*)&this_pBuffer[done];
		for (int16_t i = 0; i < frame_size; i++) {
			*this_ptr++ = (( ((INT_PCM*) pDec_data)[i] ) &0xFF00) | ((INT_PCM*) pDec_data)[i] & 0x00FF;
		}

		done += frame_size * sizeof(INT_PCM);
		frames_counter++;
    }

	if( pDec_data != NULL){
		Mfree(pDec_data);
		pDec_data = NULL;
	}
}

void st_AAC_Close(){
	faacDecClose(*pAAC_Handler);
	MP4Close(pMP4_Handler);
	processedSize = 0;
	Mfree(pBuffer);
}

u_int16_t getFirstAudioTrack(MP4FileHandle *reader) {
    u_int16_t trackCount = MP4GetNumberOfTracks(reader, NULL, 0);

    if (trackCount == 0) {
        return 0;
    }

    for (u_int16_t i = 1; i <= trackCount; i++) {
        u_int16_t type = MP4GetTrackAudioMpeg4Type(reader, i);

        if ((type == MP4_MPEG4_AAC_LC_AUDIO_TYPE) ||
            (type == MP4_MPEG4_AAC_SSR_AUDIO_TYPE) ||
            (type == MP4_MPEG4_AAC_HE_AUDIO_TYPE)) {
            return i;
        }
    }
    return 0;
}

int32_t get200hz(void)
{
	return *((int32_t*)0x4ba);
}
