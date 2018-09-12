/*
	Coherent RTLSDR. Synchronizes rtlsdr receivers to phase coherence.
	Requires the dongles to be clocked from the _same_ clock signal, possibly buffered,
	if your setup has many dongles. Also, a common noise source is required. Refer to 
	internet how you should set this up.

	Uses experimental rtl-sdr drivers, VOLK, FFTW and ZMQ.

	Mikko Laakso - Aalto University
	mikko.t.laakso@aalto.fi

	cmake folders and lists not yet done, 
	compile with:
	gcc coherentsdr.cc -lzmq -lpthread -lfftw3f -lvolk -lstdc++ -lrtlsdr -lm -o coherentsdr_or_whatever_you_want_the_output_named
*/

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <math.h>
#include <algorithm>
#include <pthread.h>
#include <signal.h>
#include <complex>

#include "coherentsdr.h"


uint32_t global_fcenter = 48000000;


//Global exit var, set in signal handler below. Any signal sets it, mainly for Ctrl^C to work
Barrier *startbarrier;
bool exit_all=false;

static void sighandler(int signum)
{
	fprintf(stderr, "\nSIGNAL, exit requested!\n");
	exit_all=true;
}

void csdrdevice::asynch_callback(unsigned char *buf, uint32_t len, void *ctx)
{
	if (ctx) {
		csdrdevice *d = (csdrdevice *)ctx;
		d->swapbuffer(buf);
	}
}

void csdrdevice::asynch_thread(csdrdevice *d)
{
	int ret;
	//csdrdevice *d = (csdrdevice *)ctx;
	//d->open(0,d->fcenter,d->samplerate);
	d->readcnt=0;
	startbarrier->Wait();
	rtlsdr_reset_buffer(d->dev);
	ret = rtlsdr_read_async(d->dev,csdrdevice::asynch_callback, (void *)d, d->asyncbufn, d->block_size);
	fprintf(stderr,"read_async exited #%d\n",d->devnum);
}
/*
void csdrdevice::dsp_thread(csdrdevice *d)
{
	float fcorr=0.0f;
	float g=0.0f;
	uint16_t peak;
	int nume;
	int numd;

	nume=0;
	numd=csdrdevice::numobjects;

	while (!exit_all){
		if (d->newdata) {
			d->channeldsp->convtosigned((const int8_t *) d->getbptr());
			d->newdata = false;
			
			if(d->isrefchannel()){
				
				//here we wait until every signal dongle thread has decremented the atomic counter variable. It's value should be zero:
				while(!csdrdevice::fftprocessed.compare_exchange_weak(nume,numd,std::memory_order_release,std::memory_order_relaxed));
				
				d->channeldsp->executefft();
				d->channeldsp->crosscorrelatefft(csdrdevice::refsdr->channeldsp->getfftptr());
			}
			else
			{	// do rms-gain estimate N times, average and finally set tuner gains to compensate:
				if(d->g_estctr>1){
					g+=d->channeldsp->gainestimate();
					d->g_estctr--;
				}
				else if (d->g_estctr==1)
				{
					d->g_estctr--;
					d->g_estdb = 20*log10(g/d->g_estavgN);
				}
				
				d->channeldsp->executefft();
				
				d->channeldsp->crosscorrelatefft(csdrdevice::refsdr->channeldsp->getfftptr());
				//if (csdrdevice::refsdr->readcnt!=d->readcnt)
				//	fprintf(stderr,"warning, differing buffer num. Ref %d, signal %d\n",csdrdevice::refsdr->readcnt,d->readcnt);
				csdrdevice::fftprocessed--; //decrement the atomic.
			}
			
			
			d->dk = d->channeldsp->findpeak() - d->nsamples;
		}
		else 
		{
			usleep(1);
		}

		//rtlsdr_set_sample_freq_correction_f(d->dev,fcorr);
		//usleep(30000);
	}
}*/

void csdrdevice::dspthreadsingle(csdrdevice *d,int ndev){
	
	uint32_t oldrc[22];
	for (int n=0;n<ndev;++n)
		oldrc[n]=0;

	while(!exit_all){
		for (int n=0;n<ndev;n++){
			if(d[n].readcnt>oldrc[n]){
				oldrc[n]=d[n].readcnt;
				d[n].estimatelag();
			}
		}
	}
}

void controlmsg_thread(ccontrolmsg *cmsg){
	uint32_t ret=0;
	while(!exit_all){
		ret=cmsg->listen();
		global_fcenter = (ret!=0) ? ret : global_fcenter;
		usleep(10000);
	}
}


// the synchronization algorithm is a bit ad-hoc atm, will have to rewrite it. We're aproaching synchronization by changing the samplerate of signal dongles,
// 'accelerating/decelerating' them, if you will, until the fractional lag/lead value dk is close to zero (under the threshold).
void csdrdevice::control_thread(csdrdevice *d)
{
	//std::chrono::high_resolution_clock::time_point t1,t2;
	float g=0.0f;
	float t_err=0.0f;
	while (!exit_all){
		if (global_fcenter!=d->fcenter){
			d->sync_achieved=false;
			d->setfcenter(global_fcenter);

		} else{
			t_err=fabs(d->dk);
		
			if (!d->sync_achieved){
				if ((t_err>sync_threshold) && (!d->isrefchannel())){
					float fcorr=0.0f;
					if (t_err>500)
						fcorr=0.001*sgn(d->dk);
					else if (t_err>50)	
						fcorr=0.0005*sgn(d->dk);
					else if (t_err>10)
						fcorr=0.0001*sgn(d->dk);
					else if (t_err>1)
						fcorr=0.00001*sgn(d->dk);
					else
						fcorr=0.0000001*sgn(d->dk);

					rtlsdr_set_sample_freq_correction_f(d->dev,fcorr);
				//	t2=t1;
		 		//	t1=std::chrono::high_resolution_clock::now();
		 		//	std::chrono::duration<double> time_span = std::chrono::duration_cast<std::chrono::duration<double>>(t2-t1);
		 		//	fprintf(stderr,"Dev #%d delta %2.6f\n",d->devnum,time_span);
				}
				else
				{
					rtlsdr_set_sample_freq_correction_f(d->dev,0.0f);
					if (d->getreadcnt()>10)
						d->sync_achieved=true;
				}
			}
			usleep(32800);
		}
	}
	fprintf(stderr,"requesting cancel_async() from callback #%d\n",d->devnum);
	rtlsdr_cancel_async((rtlsdr_dev_t *) d->getdevptr());
}


int csdrdevice::startasynchread(int ndevices,Barrier *startbar)
{
	wthread   = std::thread(csdrdevice::asynch_thread,this);
	cthread   = std::thread(csdrdevice::control_thread,this);
	//dspthread = std::thread(csdrdevice::dsp_thread,this);
	return 1;
}


void usage(void)
{
	fprintf(stderr,
		"\ncoherentsdr - synchronous rtl-sdr reader, IQ-samples published to a ZMQ-socket (atm, this is tcp://*:5555)\n"
		"Dongles need to be clocked from the same signal, otherwise cross-correlation and synchronization will result in garbage\n\n"
		"Usage:\n"
		"\t[-f frequency_to_tune_to [Hz'(default 480MHz)]\n"
		"\t[-s samplerate (default: 2048000 Hz)]\n"
		"\t[-n number of devices to init\n"
		"\t[-S use synchronous read (default: async) NOT FUNCTIONAL CURRENTLY]\n"
		"\t[-g tuner gain: signal [dB] (default 60)]\n"
		"\t[-r tuner gain: reference [dB] (default 50)]\n"
		"\t[-I reference dongle serial ID (default 'MREF')]\n"
		"\t[-A set automatic gaincontrol for all devices]\n");
	exit(1);
}



int main(int argc, char **argv){

	int opt;
	char *socketstr = NULL;
	uint32_t ndevices = rtlsdr_get_device_count();
	uint32_t refgain  = 500;
	uint32_t siggain  = 600;
	uint32_t agcmode  = 0;

	std::string refname="MREF";

	if (ndevices<1){
		fprintf(stderr,"No rtl-sdr devices found! exiting.\n");
		exit(1);
	}
	

	//signalhandling init, so the code responds to ctrl^c
	struct sigaction sigact;
	sigact.sa_handler = sighandler;
	sigemptyset(&sigact.sa_mask);
	sigact.sa_flags = 0;
	sigaction(SIGINT, &sigact, NULL);
	sigaction(SIGTERM, &sigact, NULL);
	sigaction(SIGQUIT, &sigact, NULL);
	sigaction(SIGPIPE, &sigact, NULL);

	//testcode -remove and fix commandline
	//uint32_t fs      = 2048000;
	uint32_t fs      = 1024000;
	
	bool	synchmode= false;


	while ((opt = getopt(argc, argv, "As:f:h:n:g:r:I:")) != -1) {
		switch (opt) {
		case 'A':
				agcmode=1;
			break;
		case 's':
			fs=(uint32_t)atof(optarg);
			break;
		case 'f':
			global_fcenter=(uint32_t) atof(optarg);
			break;
		case 'h':
			usage();
			break;
		case 'n':
				if ((uint32_t)atoi(optarg)<=ndevices)
					ndevices=(uint32_t)atoi(optarg);
				else fprintf(stderr,"Requested device count higher than devices connected to system (%d), setting n = %d\n",ndevices,ndevices);
			break;
		case 'g':
				siggain = (uint32_t) atoi(optarg)*10;
			break;
		case 'r':
				refgain = (uint32_t) atoi(optarg)*10;
			break;
		case 'I':
				refname=std::string(optarg);
			break;
		default:
			usage();
			break;
		}
	}

	fprintf(stderr,"Using samplerate: %d. Initial tuning frequency:%d\n",fs,global_fcenter);

	startbarrier = new Barrier(ndevices);
	csdrdevice *sdr = new csdrdevice[ndevices];
	int block_size=default_buffersize;

	
	{ //scope for packetize & controlmsg lifetime
		cpacketize packetize(ndevices,block_size, "tcp://*:5555");
		ccontrolmsg controlmsg(ndevices,"tcp://*:5556");
		csdrdevice::packetize = &packetize;

	
		fprintf(stderr,"opening reference device %s...\n",refname.data());
		int refID=rtlsdr_get_index_by_serial((const char *)refname.data());
		if (refID<0) {
				fprintf(stderr,"device with serial %s not found, using device #0 as reference (warning, this may yield incorrect results!)\n",refname.data());
				refID=0;
		}

		sdr[refID].setrefchannel();
		csdrdevice::setrefsdrptr(sdr);
		sdr[refID].setdevnum(0,global_fcenter,fs);
		if(sdr[refID].open(0,global_fcenter,fs,refgain,agcmode)<0){
			fprintf(stderr,"Failed to open device #%d\n",refID);
			}
		else{
			sdr[refID].startasynchread(ndevices,startbarrier);
		}
	
		
		for (uint32_t n=1;n<ndevices;n++){
				if (n!=refID){
					sdr[n].setdevnum(n,global_fcenter,fs);
					if(sdr[n].open(n,global_fcenter,fs,siggain,agcmode)<0){
						fprintf(stderr,"Failed to open device #%d\n",n);
					}
					else{
						sdr[n].startasynchread(ndevices,startbarrier);
					}
			}
		}
			//std::thread dspthrd(csdrdevice::dspthreadsingle,sdr,ndevices);
			std::thread controlmsgthrd(controlmsg_thread, &controlmsg);


		int np=0;
		while (!exit_all){

			bool newdata=true;
			uint32_t rcnt=sdr[0].getreadcnt();
			for(int n=0;n<ndevices;n++){
				sdr[n].waitbuf();
				//if (sdr[n].getreadcnt()!=rcnt)
					//fprintf(stderr,"warning, differing readcount ref:%d sig#%d:%d",rcnt,n,sdr[n].getreadcnt());
			}
				for (int n=0;n<ndevices;n++){
					sdr[n].estimatelag();
					sdr[n].refsubtract();
					packetize.write(n,sdr[n].getreadcnt(),(int8_t *) sdr[n].getoutbptr());	
	
					controlmsg.setlag(n,sdr[n].getlag());
					sdr[n].newdata=false;
				}
				packetize.send();
		
			/*
			fprintf(stdout,"\33[2K\r %+4.3f | %+4.3f | %+4.3f | %+4.3f | %+4.3f | %+4.3f | %+4.3f",
				sdr[0].getlagerrf()+block_size/2,sdr[1].getlagerrf(),sdr[2].getlagerrf(),sdr[3].getlagerrf(),
				sdr[4].getlagerrf(),sdr[5].getlagerrf(),sdr[6].getlagerrf());*/
			// resorting to VT-100 terminal escape sequences to print status information:
			
			if (np++ % 4 == 0){
				fprintf(stdout,"\33[2K\r");
				fprintf(stdout,"\33[1A");
				fprintf(stdout,"\33[2K\r");
				for (int n=1;n<ndevices;n++){
					if (sdr[n].sync_achieved)
						fprintf(stdout,"SYNC\t");
					else 
						fprintf(stdout,"SEEK\t");
				}
				fprintf(stdout,"\n");
				for (int n=1;n<ndevices;n++){
					fprintf(stdout,"%+7.2f\t",sdr[n].getlagerrf());
				}
			}
			

			fflush(stdout);
		}

		fprintf(stderr,"\nexit stage: 1/3. signaling threads to exit gracefully.\n");
		controlmsgthrd.join();
	} // packetize & controlmsg gets destroyed here, clean exit.
	
	for (int n=0;n<ndevices;n++){
		sdr[n].exitrequested=true;
		sdr[n].join();
	}
	

	
	fprintf(stderr,"exit stage: 2/3. closing receivers.\n");
	for (int n=0;n<ndevices;n++){
		sdr[n].close();
	}

	fprintf(stderr,"exit stage: 3/3. freeing memory.\n");
	delete [] sdr;

	delete startbarrier;
	
	return 1;
}