#ifndef COHERENTSDRH
#define COHERENTSDRH

#include <rtl-sdr.h>
#include <volk/volk.h> //included here for volk malloc. need aligned buffers for volk accelerated conversion -> float
#include <stdint.h>
#include <atomic>
#include <thread>
#include <vector>
#include <condition_variable>
#include <mutex>
#include <chrono>
#include <zmq.h>
#include "dsp.h"
#include "packetize.h"
#include "controlmsg.h"
#include <ctime>
#include <ratio>

const uint32_t default_buffersize = (4*8192); //was 4*8192

const float sync_threshold=0.005; //when dk goes below this value, synch_achieved flag will be set.

const uint32_t FFT_EVERY_N=32; // after synchronization is acquired, compute it only every N:th round.

class Barrier
{
private:
    std::mutex mtx;
    std::condition_variable cv;
    std::size_t cnt;
public:
    explicit Barrier(std::size_t cnt) : cnt{cnt} { }
    void Wait()
    {
        std::unique_lock<std::mutex> lock{mtx};
        if (--cnt == 0) {
            cv.notify_all();
        } else {
            cv.wait(lock, [this] { return cnt == 0; });
        }
    }
};


//Barrier startbarrier(22);

//buffers 'class', entiery public.
struct cbuffer{
	uint32_t						rcnt;
	int64_t						timestamp;
	uint8_t							*ptr;
	uint32_t						N;

	/*cbuffer(uint32_t block_size){
		N=block_size;
	}*/
	cbuffer(){
		N=default_buffersize;
	}

	void setbufferptr(uint8_t *buffer,uint32_t readcnt)
	{
		timestamp = std::chrono::high_resolution_clock::now().time_since_epoch().count();
		ptr=buffer;
		rcnt=readcnt;
		conv2signed();
	}

	//convert the sample values to signed in place. note that we're operating on memory provided by librtlsdr, hoping to get away with it in time. 
	//this should be called immediately or atleast soon after receiving a buffer pointer from asynch_read.
	void conv2signed()
	{
		uint32_t n64bitvals=N/8;
		uint64_t *p64 = (uint64_t *) ptr;

		//do the 's XOR 128' with 64 bit values, operating on 8 samples at a time: (compiler may or may not optimize code operating on bytes to this)
		for (uint32_t n=0;n<n64bitvals;++n)
			*(p64 + n) = (*(p64 +n ))^0x8080808080808080;
	}	

};

class csdrdevice{
	uint32_t	 		 			devnum;			//rtlsdr device params
	uint32_t			 			asyncbufn;
	uint32_t	 		 			samplerate;
	uint32_t						fcenter;
	int 				 			block_size;
	int 							nsamples;	
	rtlsdr_dev_t 		 			*dev;
	bool							refchannel;

	cdsp							*channeldsp;

	//buffers:
	cbuffer							*rbuf;

	lv_32fc_t						*g_estbuf;


	std::thread 					wthread;
	std::thread 					cthread;
	std::thread 					dspthread;

	std::mutex 						mtx;
	std::condition_variable 		cv;
	std::condition_variable			tcv;


	uint32_t						readcnt;
public:
	bool							sync_achieved;
	std::atomic<bool>				newdata;
	static std::atomic<int>			fftprocessed;
	static cpacketize				*packetize;
private:
	float		 					dk;  	 //used to be 'volatile', however this does not mean much for modern compilers.
	float 							dk_errf; //leaky integrated lag/lead absolute error.
	float 							dk_errfp;
	std::atomic<int>				g_estctr;

	float							g_estdb;
	int 							g_estavgN;
	bool							g_set;

	uint32_t						fftcnt;
	uint32_t 						tunergain;

	static csdrdevice				*refsdr;
	
	static int 						numobjects;


public:

	bool							readsynchronous;
	bool							exitrequested; //used to be 'volatile'
	
	csdrdevice(){
		int alignment = volk_get_alignment();
		
		block_size=default_buffersize;
		nsamples  = block_size/2;
		g_estbuf = (lv_32fc_t *) volk_malloc(sizeof(lv_32fc_t)*nsamples,alignment);

		exitrequested 	= false;
		readsynchronous = false;
		refchannel 		= false;
		newdata			= false;
		sync_achieved 	= false;
		asyncbufn 		= 4;
		
		g_estavgN 		= 10;
		g_estctr  		= g_estavgN;

		g_estdb		    = -50;
		g_set 			= false;
		tunergain 		= 500;

		rbuf 			= new cbuffer[asyncbufn];
	
		channeldsp = new cdsp(block_size,false);
		numobjects++;

		fftcnt 			= numobjects; //now the counters are out of phase, ensuring that we don't hit FFTeveryNth all on same buffer cycle.

		dk 				=0.0f;
		dk_errf 		=0.0f;
		dk_errfp 		=0.0f;
		fftcnt 			=0;
	}

	 ~csdrdevice(){
	 	volk_free(g_estbuf);
	 	//delete wthread;
	 	//delete cthread;
	 	delete [] rbuf;
	 	delete channeldsp;
	 }

	 uint32_t setdevnum(uint32_t dn,uint32_t fc,uint32_t fs){
	 	devnum=dn;
	 	fcenter=fc;
	 	samplerate=fs;
	 }

	 int setfcenter(uint32_t f){
	 	fcenter=f;
	 	return rtlsdr_set_center_freq(dev,fcenter);
	 }

	 int open(uint32_t ndevice,uint32_t fcenter, uint32_t samplerate,uint32_t gain,uint32_t agcmode){
	 	//devnum=ndevice;
	 	tunergain=gain;

	 	int ret=rtlsdr_open(&dev,devnum);
	 	ret = rtlsdr_set_sample_rate(dev, samplerate);
	 	ret = rtlsdr_set_center_freq(dev, fcenter);

	 	ret = rtlsdr_set_agc_mode(dev, agcmode);
		//ret = rtlsdr_set_if_freq(dev, uint32_t(36000000));
		ret = rtlsdr_set_dithering(dev, 0);

 		ret = rtlsdr_set_tuner_gain_mode(dev, 1);
 		ret = rtlsdr_set_tuner_gain(dev,tunergain);
 		/*if (!refchannel){
 			//ret=rtlsdr_set_tuner_if_gain(dev, 1, 100);
 			ret = rtlsdr_set_tuner_gain(dev, 600);	//was 600
 		}
 		else
 			ret = rtlsdr_set_tuner_gain(dev, 500);	//was 500
 		*/
 		//testing remove everything after this line
 		//uint32_t freq, tfreq;
 		//rtlsdr_get_xtal_freq(dev, &freq, &tfreq);
 		//fprintf(stderr,"dev->xtal = %d , tuner: %d \n",freq,tfreq);
	 }

	 int close(){
	 	return rtlsdr_close(dev);
	 }

	 const rtlsdr_dev_t *getdevptr(){
		return dev; 	
	 }

	 //thread functions
	 //static void worker_thread(csdrdevice *d);
	 static void asynch_thread(csdrdevice *d);
	 static void control_thread(csdrdevice *d);
	//static void dsp_thread(csdrdevice *d);

	 static void dspthreadsingle(csdrdevice *, int);

	 //callback function
	 static void asynch_callback(unsigned char *buf, uint32_t len, void *ctx);

	 int startsynchronousread(int ndevices,Barrier *startbar);
	 int startasynchread(int ndevices, Barrier *startbar);

	 uint8_t *swapbuffer(uint8_t *b)
	 {
	 	rbuf[readcnt % asyncbufn].setbufferptr(b,readcnt);
	 	//packetize->write(devnum,readcnt,(int8_t*)b);
	 	readcnt++;
	 	{
	 		std::unique_lock<std::mutex> lock(mtx);
	 		newdata=true;
	 	}
	 	cv.notify_all();
	 	tcv.notify_all();

	 	//t2=t1;
	 	//t1=std::chrono::high_resolution_clock::now();
	 	//std::chrono::duration<double> time_span = std::chrono::duration_cast<std::chrono::duration<double>>(t2-t1);
	 	//fprintf(stderr,"Dev #%d delta %2.6f\n",devnum,time_span);
	 }

	 void waittcv()
	 {
	 	std::unique_lock<std::mutex> lock(mtx);
	 	tcv.wait(lock, [this]{return newdata.load();});
	 }

	 void waitbuf()
	 {
	 	std::unique_lock<std::mutex> lock(mtx);
	 	cv.wait(lock, [this]{return newdata.load();});
	 	newdata=false;
	 }
	 int join()
	 {

	 	//dspthread.join();
	 	wthread.join();
	 	cthread.join();
	 }

	 void setrefchannel()
	 {
	 	refchannel=true;
	 	channeldsp->setrefchannel();
	 }

	 bool isrefchannel(){
	 	return refchannel;
	 }

	 static void setrefsdrptr(csdrdevice *ref)
	 {
	 	refsdr = ref;
	 }

	 float estimatelag()
	 {
	 	float alpha=0.25f; //kind of experimental leaky integrator param. to smooth values for determining synch quality.
	 	
	 	channeldsp->convtofloat((const int8_t *)getbptr());
	 	if ((!sync_achieved)||(refchannel)||(fftcnt++ % FFT_EVERY_N)==0){
		 	//channeldsp->convtosigned((const int8_t *) getbptr());
		 	channeldsp->executefft();
		 	channeldsp->crosscorrelatefft(csdrdevice::refsdr->channeldsp->getfftptr());
		 	dk=float(channeldsp->findfracpeak())-nsamples;

		 	dk_errf  = alpha*dk+(1-alpha)*dk_errfp;
		 	dk_errfp = dk_errf;
	 	}
	 	return dk;
	 }

	 void refsubtract(){
	 	channeldsp->refsubtract(csdrdevice::refsdr->channeldsp->getsptr());
	 	channeldsp->convto8bit();
	 }

	 void convto8bit(){
	 	channeldsp->convto8bit();
	 }

	 float getlag(){
	 	return dk;
	 }

	 float getlagerrf(){
	 	return dk_errf;
	 }

	 uint8_t *getbptr(){
	 	return rbuf[(readcnt-1) % asyncbufn].ptr;
	 }

	 int8_t *getoutbptr(){
	 	return channeldsp->samples8bit;
	 }

	 uint32_t getreadcnt(){
	 	return readcnt;
	 }

	 float gainestimate(uint8_t *buf){
	 	std::complex<float> dotprod=0;

	 	//convert 8-bit samples to complex float, interleaved data. cast output g_estbuf to float for this. should work. data is interleaved floats Re and Im parts.

	 	//FIXME: this needs to be fixed, we're writing to the buffers provided by rtlsdr.: (why oh why does it output unsigned samples, and not 2's compl)
	 	int8_t *b0 = (int8_t *) buf;

	 	for (int n=0;n<block_size;n++)
	 		b0[n]=b0[n]-127;
	 	volk_8i_s32f_convert_32f((float *) g_estbuf, (const int8_t *) buf,127.0f,block_size);

	 	//multiply each sample by it's conjugate and sum the products
	 	volk_32fc_x2_conjugate_dot_prod_32fc((lv_32fc_t*) &dotprod,g_estbuf,g_estbuf,nsamples);

	 	//lv_32fc_t should be bit-compatible with std::complex<float>. After above dotproduct, Imag parts should be zero.
	 	return sqrt(dotprod.real()/float(nsamples));
	 }

};

int csdrdevice::numobjects=0;
csdrdevice * csdrdevice::refsdr;
std::atomic<int> csdrdevice::fftprocessed(0); 
cpacketize * csdrdevice::packetize=NULL;

template <typename T> int sgn(T val) {
    return (T(0) < val) - (val < T(0));
}

#endif
