#ifndef DSPH
#define DSPH

#include <volk/volk.h> //included here for volk malloc. need aligned buffers for volk accelerated conversion -> float
#include <fftw3.h>
#include <stdint.h>

class cdsp{

	fftwf_plan		fftplan,ifftplan;

	
	lv_32fc_t 		*samples;
	lv_32fc_t 		*samplesout;
	fftwf_complex 	*fftout;
	fftwf_complex   *ifftout;
	lv_32fc_t		*conv;
	float			*magsqr;
	float			*mref;
	uint16_t		*maxc;

	bool			refchannel;
	int				block_size;
	int				nsamples;

	float			lag;

public:
	int8_t			*samples8bit;

cdsp(int bsize,bool isrefchan){
	
	block_size = bsize;
	nsamples   = bsize/2;
	refchannel = isrefchan;


	// fftwf3 and VOLK need their memory aligned, otherwise they may not offer speedup. 
	// the binary representations of these complex types, lv_32_fc_t are binary compatible with
	// interleaved float representation of cmplx (Re0,Im0,Re1,Im1...) , e.g. std::complex<float>
	// The alignment of volk_malloc and fftwf_malloc is (most likely) the same, these could be interchanged.
	int alignment = volk_get_alignment();

	samples8bit	= (int8_t *) volk_malloc(sizeof(int8_t)*block_size,alignment);
	samples 	= (lv_32fc_t *) volk_malloc(sizeof(lv_32fc_t)*block_size,alignment);
	samplesout 	= (lv_32fc_t *) volk_malloc(sizeof(lv_32fc_t)*block_size,alignment);

	conv 		= (lv_32fc_t *) volk_malloc(sizeof(lv_32fc_t)*block_size,alignment);
	magsqr 		= (float *) volk_malloc(sizeof(float)*block_size,alignment);
	maxc 		= (uint16_t *) volk_malloc(sizeof(uint16_t)*block_size,alignment);
	mref 		= (float *) volk_malloc(sizeof(float)*block_size,alignment);

	fftout  = fftwf_alloc_complex(block_size);
	ifftout = fftwf_alloc_complex(block_size);

	memset(samples,0,sizeof(lv_32fc_t)*block_size);
	memset(conv,0,sizeof(lv_32fc_t)*block_size);

	// init fft plans, test-execute one round on empty mem:
	fftplan = fftwf_plan_dft_1d(block_size,(fftwf_complex *)  (samples), fftout ,FFTW_FORWARD,0);
	ifftplan = fftwf_plan_dft_1d(block_size,(fftwf_complex *) (conv), ifftout,FFTW_BACKWARD,0);
	fftwf_execute(fftplan);
	fftwf_execute(ifftplan);
}
~cdsp(){
	volk_free(samples);
	volk_free(samplesout);
	volk_free(conv);
	volk_free(magsqr);
	volk_free(maxc);
	volk_free(samples8bit);
	volk_free(mref);

	fftwf_free(fftout);
	fftwf_free(ifftout);
	fftwf_destroy_plan(fftplan);
	fftwf_destroy_plan(ifftplan);
}

 void setrefchannel()
{
	 	refchannel=true;
}

// call the functions in order convtosigned(samples), executefft(), crosscorrelatefft(refft), findpeak()
// for noise channel, naturally only contosigned and executefft are needed. The result of this fft is then
// used by signal channels crosscorrelatefft()
int convtosigned(const int8_t * su8bit)
{
	//now the compiler may or may not optimize this to use 32 or 64 -bit pointers, xorring multiple samples (4, or 8) simultaneously
	//should check, if not, then write the code manually
	for(int i=0;i<block_size;++i)
		samples8bit[i]=su8bit[i] ^ 0x80;	// sample xor 128, for 8 bit unsigned, this produces same result as smp-128;

	//for crosscorrelate, the samples are set up in double length buffers, where the other half is zeros, to simulate convolution to inf
	//if this is the noise reference channel, we copy the sample data after block_size of zeros, otherwise we copy it to the beginning, zeros follow
	//also note: this is not the exact right way to do sample conv, we should be adding 0.5f and after that, round to float. int8_t range is [-128,127]

	//safety measure, this zeroing should not be needed:	
	memset((samples),0,sizeof(lv_32fc_t)*block_size);

	if (refchannel)
		volk_8i_s32f_convert_32f((float *) samples + block_size,samples8bit,127.0f,block_size);
	else
		volk_8i_s32f_convert_32f((float *) samples,samples8bit,127.0f,block_size);
}

int convtofloat(const int8_t *s8bit){
	if (refchannel)
		volk_8i_s32f_convert_32f((float *) samples + block_size,s8bit,127.0f,block_size);
	else
		volk_8i_s32f_convert_32f((float *) samples,s8bit,127.0f,block_size);

}

int convto8bit()
{
	//ideally, here we would do for each sample index s8bit=round(127.0f*s32bitfloat-0.5f)
	//however, there is no add/subtract scalar to/from vector volk kernel: we're losing one value out of 256: -128.
	volk_32f_s32f_convert_8i(samples8bit,(float *)samples,127.0f,block_size);
}

int executefft()
{	
	fftwf_execute_dft(fftplan,(fftwf_complex*) samples,fftout);
}

const fftwf_complex* getfftptr() {
	return fftout;
}

const lv_32fc_t* getsptr(){
	return samples+nsamples;
}

int crosscorrelatefft(const fftwf_complex *reffft){
	volk_32fc_x2_multiply_conjugate_32fc(conv,(const lv_32fc_t *) fftout,(const lv_32fc_t *) reffft,block_size);
	fftwf_execute_dft(ifftplan,(fftwf_complex *) conv,ifftout);
}

void refsubtract(const lv_32fc_t *ref){
	float refpower=0.0f;
	std::complex<float> correlation=0.0f;
	std::complex<float> correction =0.0f;
	float refsubtract;

	//estimate refnoise power:
	volk_32f_x2_dot_prod_32f(&refpower, (const float *)ref, (const float *)ref,block_size);

	//take the dotprod, correlation at lag=0, assuming channels are now timesynch. w.r.t. refchannel
	volk_32fc_x2_conjugate_dot_prod_32fc((lv_32fc_t *) &correlation,samples,ref,nsamples);

	correction  = std::conj(correlation) * (1.0f /std::abs(correlation));
	refsubtract = -std::abs(correlation) / std::abs(refpower);

	volk_32fc_s32fc_multiply_32fc(samplesout,samples,correction,nsamples);
	volk_32f_s32f_multiply_32f(mref,(float *)ref,refsubtract,block_size);
	volk_32f_x2_add_32f((float *)samplesout,(float *)samplesout,mref,block_size);

	//for now just overwrite samples (the floating point sample data) as it is not used anymore for this buffer:
/*	volk_32fc_s32fc_multiply_32fc(samples,samples,correction,nsamples);
	volk_32f_s32f_multiply_32f(mref,(float *)ref,refsubtract,block_size);
	volk_32f_x2_add_32f((float *)samples,(float *)samples,mref,block_size);
*/
}

uint16_t findpeak(){
	uint32_t maxc;

	//magnitude squared, find peak. Newer versions of VOLK have index_max_32u, here I had to resort to index_max_16u
	//this limits the maximum offset to 65535.
	volk_32fc_magnitude_squared_32f(magsqr,(const lv_32fc_t *) ifftout,block_size);
	volk_32f_index_max_32u(&maxc,magsqr,block_size);
	return maxc;
}


// this version of peakfinder returns the index of the peak as float, where the fractional sample-index is 
// calculated from first derivative of Lagrange polynomial (k=3).
float findfracpeak(){
	uint32_t n;
	float D=0.0f;
	float a=0.0f;
	float b=0.0f;

	volk_32fc_magnitude_squared_32f(magsqr,(const lv_32fc_t *) ifftout,block_size);
	volk_32f_index_max_32u(&n,magsqr,block_size);

	//check if we're at the edge (samples n-1, n+1 needed), if not return only int.part
	if ((n>1) && (n<block_size-1)){
	a=-magsqr[n-1]-2*magsqr[n]+magsqr[n+1];
	b=-0.5f*magsqr[n-1]+0.5f*magsqr[n+1];

	//handle possible divide by zero by setting the fractional to zero:
	if (a!=0.0f)
		D=-b/a;
	else
		D=0.0f;
	return D+n;
	}
	else return n;
}

float gainestimate(){
	 	std::complex<float> dotprod=0;

	 	//multiply each sample by it's conjugate and sum the products
	 	volk_32fc_x2_conjugate_dot_prod_32fc((lv_32fc_t*) &dotprod,samples,samples,block_size);

	 	//lv_32fc_t should be bit-compatible with std::complex<float>. After above dotproduct, Imag parts should be zero.
	 	return sqrt(dotprod.real()/float(nsamples));
	 }

float getlag(){
	return lag;
}

};

#endif