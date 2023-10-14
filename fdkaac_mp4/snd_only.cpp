/* .aac Atari player demo by MedMed and OL */

#include <stdio.h>
#include <string.h>
#include <sys/types.h>

#include <mint/osbind.h>
#include <mint/mintbind.h>
#include <mint/falcon.h>

// #include "./wav_lib/wavreader.h"
#include <fdk-aac/aacdecoder_lib.h>
#include <mp4v2/mp4v2.h>

#ifndef cdecl
#define cdecl __CDECL
#endif

#ifndef SIGUSR1
#define SIGTERM 15
#define SIGUSR1 29
#endif				
									
#define min(a,b) (a<=b?a:b)
#define max(a,b) (a>=b?a:b)

int16_t attenuation_left, attenuation_right;
int16_t loadNewSample;
int16_t clk_prescale;
int8_t* pPhysical;
int8_t* pLogical;
MP4FileHandle* pMP4_Handler;
HANDLE_AACDECODER* pAAC_Handler;
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
	    // bufferSize = sample_rate * bits_per_sample * channels;
	    pBuffer = NULL;
	    pBuffer = (int8_t*)Mxalloc(bufferSize << 1, 0); /* Fois 2 parceque double buffer de son */

	    /* Pointeurs vers le double buffer son */
	    pPhysical = pBuffer;
 	    pLogical = pBuffer + bufferSize;

	    /* On rempli les deux buffer avec la data PCM */
	    st_Snd_LoadBuffer( pPhysical );
    	/*    st_Snd_LoadBuffer( pLogical );*/

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
	   /* while(!loadNewSample) Syield();
	    Syield();*/
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

	// wav_read_close(wav);
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
    AAC_DECODER_ERROR snd_err;
	pAAC_Handler = (HANDLE_AACDECODER*)Mxalloc(sizeof(HANDLE_AACDECODER), 3);
	*pAAC_Handler = aacDecoder_Open(TT_MP4_RAW, 1);

    Snd_track_number = getFirstAudioTrack(pMP4_Handler);
    total_frames = MP4GetTrackNumberOfSamples(pMP4_Handler, Snd_track_number);
    max_frame_size = MP4GetTrackMaxSampleSize(pMP4_Handler, Snd_track_number);
	// printf("###\tTrack Num %d\n###\tTotal frames %d\n###\tMax Frame Size %d\n",Snd_track_number, total_frames, max_frame_size);

    UINT	confBytes = 128;
    uint8_t	conf[confBytes];

    MP4GetTrackESConfiguration(pMP4_Handler, Snd_track_number, (uint8_t **)&conf, (uint32_t *)&confBytes);
    snd_err = aacDecoder_ConfigRaw(*pAAC_Handler, (uint8_t **)&conf, (uint32_t *)&confBytes);

	if ((snd_err = aacDecoder_SetParam(*pAAC_Handler, AAC_PCM_MIN_OUTPUT_CHANNELS, 2)) != AAC_DEC_OK) {
		printf("Couldn't set min output channels: %d", snd_err);
	}
	if ((snd_err = aacDecoder_SetParam(*pAAC_Handler, AAC_PCM_MAX_OUTPUT_CHANNELS, 2)) != AAC_DEC_OK) {
		printf("Couldn't set max output channels: %d", snd_err);
    }

    UINT    packet_size = max_frame_size; 
	uint8_t  pData[packet_size], *ptr = pData;

    MP4ReadSample(pMP4_Handler, Snd_track_number, 1, (uint8_t **) &ptr, &packet_size, NULL, NULL, NULL, NULL);

	UINT bytes_valid = packet_size;

	processedSize = 0;

	snd_err = aacDecoder_Fill(*pAAC_Handler, &ptr, &packet_size, &bytes_valid);
	if (snd_err != AAC_DEC_OK) {
		fprintf(stderr, "Fill failed: %x\n", snd_err);
	}

	u_int16_t dec_size = 8 * 2048 * sizeof(INT_PCM);
    INT_PCM* pDec_data = NULL;
	pDec_data = (INT_PCM*)Mxalloc(dec_size, 3);
	if( pDec_data == NULL){
		fprintf(stderr, "ERROR: AAC - pDec_data malloc");
	}

	snd_err = aacDecoder_DecodeFrame(*pAAC_Handler, (INT_PCM *)pDec_data, dec_size / sizeof(INT_PCM), 0);
	if (snd_err == AAC_DEC_NOT_ENOUGH_BITS){
		fprintf(stderr, "AAC_DEC_NOT_ENOUGH_BITS : %x\n", snd_err);
	}
	if (snd_err != AAC_DEC_OK) {
		fprintf(stderr, "Decode failed: %x\n", snd_err);
	}
	CStreamInfo*    snd_info = aacDecoder_GetStreamInfo(*pAAC_Handler);
	sample_rate = MP4GetTrackTimeScale(pMP4_Handler, Snd_track_number);
	track_duration = double(MP4ConvertFromTrackDuration(pMP4_Handler, Snd_track_number, MP4GetTrackDuration(pMP4_Handler, Snd_track_number), MP4_MSECS_TIME_SCALE));
	clk_prescale = (((25175000 >> 8) / snd_info->sampleRate) - 1) ;

	if( pDec_data != NULL){
		Mfree(pDec_data);
		pDec_data = NULL;
	}

	frames_counter = 1;
	printf( "\n###\tOriginal Sample Rate %dHz\n###\tTotal duration of track: %fs\n###\tPrescale: %d\n", sample_rate, track_duration/1000, clk_prescale );
}

void st_AAC_Decode( int8_t* this_pBuffer ){

    AAC_DECODER_ERROR   snd_err;

    int16_t  frame_size = 0;
    u_int32_t local_frames_counter = 0;

    UINT    bytes_valid;
  
    u_int16_t dec_size = 8 * 2048 * sizeof(INT_PCM);
    INT_PCM* pDec_data = NULL;
	pDec_data = (INT_PCM*)Malloc(dec_size);
	if( pDec_data == NULL){
		fprintf(stderr, "ERROR: AAC - pDec_data malloc");
	}
	u_int32_t done = 0;


    while( done < bufferSize && frames_counter < total_frames) {

		UINT    packet_size = max_frame_size; 
		u_int8_t  pData[packet_size], *ptr = pData;

		MP4ReadSample(pMP4_Handler, Snd_track_number, frames_counter, (uint8_t **) &ptr, &packet_size, NULL, NULL, NULL, NULL);

        bytes_valid = packet_size;
		snd_err = aacDecoder_Fill(*pAAC_Handler, &ptr, &packet_size, &bytes_valid);
		if (snd_err != AAC_DEC_OK) {
			fprintf(stderr, "Fill failed: %x\n", snd_err);
		}
		snd_err = aacDecoder_DecodeFrame(*pAAC_Handler, (INT_PCM *)pDec_data, dec_size / sizeof(INT_PCM), 0);
		if (snd_err == AAC_DEC_NOT_ENOUGH_BITS){
			fprintf(stderr, "AAC_DEC_NOT_ENOUGH_BITS : %x\n", snd_err);
			processed_done = true;
			return;
		}
		if (snd_err != AAC_DEC_OK) {
			fprintf(stderr, "Decode failed: %x\n", snd_err);
		}

        CStreamInfo*    snd_info = aacDecoder_GetStreamInfo(*pAAC_Handler);
        frame_size = snd_info->frameSize * snd_info->numChannels;
	
        // u_int8_t* tmp_ptr = (u_int8_t*)&this_pBuffer[done];


		u_int16_t* this_ptr = (u_int16_t*)&this_pBuffer[done];
		for (int16_t i = 0; i < frame_size; i++) {
			*this_ptr++ = (( ((INT_PCM*) pDec_data)[i] ) &0xFF00) | ((INT_PCM*) pDec_data)[i] & 0x00FF;
		}

		// if( is_destination_BE == TRUE ){
		// 	for (int16_t i = 0; i < frame_size; i++) {
		// 	u_int8_t* out = &tmp_ptr[sizeof(INT_PCM) * i];
		// 		out[0] = (u_int8_t)( ((pDec_data[i] ) >> 8) );
		// 		out[1] = (u_int8_t)( pDec_data[i] );
		// 	}
		// } else {
		// 	for (int16_t i = 0; i < frame_size; i++) {
		// 	u_int8_t* out = &tmp_ptr[sizeof(INT_PCM)*i];
		// 	u_int16_t j;
		// 	for (j = 0; j < sizeof(INT_PCM); j++)
		// 		out[j] = (u_int8_t)( pDec_data[i] >> ( 8 * j ) );
		// 	}
		// }

		done += frame_size * sizeof(INT_PCM);
		local_frames_counter += 1;
		frames_counter++;
    }

	if( pDec_data != NULL){
		Mfree(pDec_data);
		pDec_data = NULL;
	}
}

void st_AAC_Close(){
	aacDecoder_Close(*pAAC_Handler);
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
