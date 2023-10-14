/* .aac Atari player demo by MedMed and OL */

#include <stdio.h>
#include <string.h>
#include <sys/types.h>

#include <mint/osbind.h>
#include <mint/mintbind.h>
#include <mint/falcon.h>

#include <mad.h>
#include <sys/stat.h>

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

mad_decoder *handle;
mad_stream	Stream;
mad_frame	Frame;
mad_synth	Synth;
mad_timer_t	Timer;
u_int8_t 	*InputBuffer;
u_int32_t	mp3_file_size;
FILE		*soundfile_fd;

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

static void scan_file (FILE * fd, u_int32_t *length, u_int32_t *bitrate, u_int32_t *samplerate, u_int32_t *nb_channels );
static int16_t MadFixedToSshort(mad_fixed_t Fixed);
typedef struct Dither Dither;
struct Dither
{
	mad_fixed_t error[3];
	mad_fixed_t random;
};
#define PRNG(x) (((x)*0x19660dL + 0x3c6ef35fL) & 0xffffffffL)

enum
{
	FracBits = MAD_F_FRACBITS,
	OutBits = 16,
	Round = 1 << (FracBits+1-OutBits-1),	// sic
	ScaleBits = FracBits + 1 - OutBits,
	LowMask  = (1<<ScaleBits) - 1,
	Min = -MAD_F_ONE,
	Max = MAD_F_ONE - 1,
};
int audiodither(mad_fixed_t v, Dither *d);

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
	struct stat file_stat;
	stat(soundfile, &file_stat);

	mp3_file_size = file_stat.st_size;

	InputBuffer = (u_int8_t*)Mxalloc(mp3_file_size + MAD_BUFFER_GUARD, 3);

	soundfile_fd = fopen(soundfile, "rb");
	u_int32_t src_brate, src_duration, src_samplerate, src_channel;
	scan_file(soundfile_fd,&src_duration, &src_brate,   &src_samplerate, &src_channel);
	fclose(soundfile_fd);
	printf("src_duration %lu src_channel %lu\n", src_duration, src_channel);

	soundfile_fd = fopen(soundfile, "rb");
	fread(InputBuffer, 1, mp3_file_size, soundfile_fd);
	fclose(soundfile_fd);

	/* First the structures used by libmad must be initialized. */
	mad_stream_init(&Stream);
	mad_frame_init(&Frame);
	mad_synth_init(&Synth);
	mad_timer_reset(&Timer);

	mad_stream_buffer(&Stream, InputBuffer, mp3_file_size);

	printf("MP3 File size %lu\n", mp3_file_size);

	off_t length = ((src_duration / 1000) * src_samplerate * src_channel );

	track_duration = (double)src_duration;
	printf("track_duration %f\n", track_duration);
	total_length = length << 1;

	printf("total length %lu\n", total_length);

    sample_rate = src_samplerate;
	channels = src_channel;
	processedSize = 0;

	clk_prescale = (((25175000 >> 8) / sample_rate) - 1) ;

	printf( "\n###\tUsing libmad\n");
	printf( "\n###\tOriginal Sample Rate %dHz\n###\tTotal duration of track: %fs\n###\tPrescale: %d\n", sample_rate, track_duration / 1000, clk_prescale );
}

void st_MP3_Decode( int8_t* this_pBuffer ){

	u_int32_t done = 0;
	short *ptr = (short*)this_pBuffer;
    while( done < bufferSize) {
		size_t current_done = 0;
		mad_frame_decode(&Frame,&Stream);	
		mad_synth_frame(&Synth,&Frame);		
		int j, n, v;
		mad_fixed_t const *left, *right;
		static Dither d;
		n = Synth.pcm.length;
		switch(Synth.pcm.channels){
		case 1:
			left = Synth.pcm.samples[0];
			for(j=0; j<n; j++){
				v = audiodither(*left++, &d);
				/* stereoize */
				*ptr++ = v;
				*ptr++ = v;
				current_done += 4;
			}
			break;
		case 2:
			left = Synth.pcm.samples[0];
			right = Synth.pcm.samples[1];
			for(j=0; j<n; j++){
				*ptr++ = audiodither(*left++, &d);
				*ptr++ = audiodither(*right++, &d);
				current_done += 4;
			}
			break;
		}
		done += current_done;
	}
}

void st_MP3_Close(){
	mad_synth_finish(&Synth);
	mad_frame_finish(&Frame);
	mad_stream_finish(&Stream);
	processedSize = 0;
	Mfree(InputBuffer);
	Mfree(pBuffer);
}

int32_t get200hz(void)
{
	return *((int32_t*)0x4ba);
}

static void scan_file (FILE * fd, u_int32_t *length, u_int32_t *bitrate, u_int32_t *samplerate, u_int32_t *nb_channels )
{
    struct mad_stream stream;
    struct mad_header header;
    mad_timer_t timer;
    unsigned char buffer[8192];
    unsigned int buflen = 0;

	bool bitrate_set = false, samplerate_set = false, channel_set = false;

    mad_stream_init (&stream);
    mad_header_init (&header);

    timer = mad_timer_zero;

    while (1)
    {
    if (buflen < 8192)
    {
            int bytes = 0;

            bytes = fread (buffer + buflen, 1, 8192 - buflen, fd);
            if (bytes <= 0)
        break;

            buflen += bytes;
    }

    mad_stream_buffer (&stream, buffer, buflen);

    while (1)
    {
		if (mad_header_decode (&header, &stream) == -1)
		{
			if (!MAD_RECOVERABLE (stream.error))
						break;
			continue;
		}
		if (length)
        mad_timer_add (&timer, header.duration);

    }

	if(bitrate && !bitrate_set){
		*bitrate = header.bitrate;
		bitrate_set = true;
	}

	if(samplerate && !samplerate_set){
		*samplerate = header.samplerate;
		samplerate_set = true;
	}

	if(nb_channels && !channel_set){
		*nb_channels = MAD_NCHANNELS(&header);
		channel_set = true;
	}

    if (stream.error != MAD_ERROR_BUFLEN)
            break;

    memmove (buffer, stream.next_frame, &buffer[buflen] - stream.next_frame);
    buflen -= stream.next_frame - &buffer[0];
    }

    mad_header_finish (&header);
    mad_stream_finish (&stream);

    if (length)
    *length = mad_timer_count (timer, MAD_UNITS_MILLISECONDS);
}

/*
 * Dither 28-bit down to 16-bit.  From mpg321. 
 * I'm skeptical, but it's supposed to make the
 * samples sound better than just truncation.
 */

int audiodither(mad_fixed_t v, Dither *d)
{
	int out;
	mad_fixed_t random;

	/* noise shape */
	v += d->error[0] - d->error[1] + d->error[2];
	d->error[2] = d->error[1];
	d->error[1] = d->error[0] / 2;
	
	/* bias */
	out = v + Round;
	
	/* dither */
	random = PRNG(d->random);
	out += (random & LowMask) - (d->random & LowMask);
	d->random = random;
	
	/* clip */
	if(out > Max){
		out = Max;
		if(v > Max)
			v = Max;
	}else if(out < Min){
		out = Min;
		if(v < Min)
			v = Min;
	}
	
	/* quantize */
	out &= ~LowMask;
	
	/* error feedback */
	d->error[0] = v - out;
	
	/* scale */
	return out >> ScaleBits;
}