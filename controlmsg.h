#ifndef CONTROMSGH
#define CONTROLMSGH
//#include <stdint.h>
#include <zmq.hpp>
#include <string>

const uint32_t CONTROLMSG_MAGIC=(('C'<<24) & ('T'<<16) & ('R'<<8) & 'L');
const uint32_t CONTROLMSG_SETFCENTER=0x01;
const uint32_t CONTROLMSG_QUERYLAGS =0x02;
const uint32_t CONTROLMSG_QUERYINFO =0x03;

class ccontrolmsg{
	zmq::context_t 		context;
	zmq::socket_t  		socket;
	zmq::message_t 		message;

	float				*lags;
	uint32_t			nchannels;
	uint32_t 			fcenter;
	uint32_t  			blocksize;
	uint32_t			sample_rate;

public:
	ccontrolmsg(uint32_t N, uint32_t L, uint32_t f, uint32_t samplerate, std::string address) : context(1), socket(context,ZMQ_ROUTER) {
		nchannels = N;
		blocksize = L;
		sample_rate=samplerate;
		fcenter   = f;
		lags 	  = new float[nchannels];
		socket.bind(address);
	}

	~ccontrolmsg(){
		delete [] lags;
	}

	int setinfo(uint32_t n, float dk, uint32_t g_fcenter)
	{
		if ((n>=0) & (n<nchannels))
			lags[n]=dk;

		fcenter = g_fcenter;
	}

	uint32_t listen(){
		size_t 		len;
		uint32_t	*msg;
		uint32_t 	ret=0;

		socket.recv(&message,ZMQ_NOBLOCK);
		msg = static_cast<uint32_t*>(message.data());
		len = message.size()/sizeof(uint32_t);

		if (len>2)
			if (msg[0]==CONTROLMSG_MAGIC){
				switch (msg[1]) {
					case CONTROLMSG_SETFCENTER:
						fprintf(stderr,"\nControl message received, tuning all receivers to %2.2f MHz\n\n",msg[2]/1e6);
						ret=msg[2];
					break;
					case CONTROLMSG_QUERYLAGS:
					{
						zmq::message_t reply(nchannels*sizeof(float));
						std::memcpy(message.data(),lags,nchannels*sizeof(float));
					}
					break;
					case CONTROLMSG_QUERYINFO:
					{
						uint32_t info[4];
						info[0] = nchannels;
						info[1] = blocksize;
						info[2] = sample_rate;
						info[3] = fcenter;
						zmq::message_t reply(sizeof(info));
						std::memcpy(message.data(),info,sizeof(info));

					}
					break;
					default:
					break;
				}

			}
	return ret;
	}

};


#endif
