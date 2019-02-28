//Coherent RTL-SDR mex interface for matlab.
//Mikko Laakso
#include <iostream>
#include <zmq.hpp>
#include "mex.hpp"
#include "mexAdapter.hpp"
#include "../controlmsg.h"
#include "../packetize.h"

using matlab::mex::ArgumentList;
using namespace matlab::data;
using matlab::engine::convertUTF8StringToUTF16String;
using matlab::engine::convertUTF16StringToUTF8String;

static bool initialized=false;

const std::complex<float> i(0,1);
const std::complex<float> o(0,0);

class MexFunction : public matlab::mex::Function{ // public matlab::data::TypedArray<std::complex<float>>
private:
		zmq::context_t 		contextr, contextc;
		zmq::socket_t  		socketr, socketc;
		std::string         addressr;
		std::string         addressc;

        std::shared_ptr<matlab::engine::MATLABEngine> matlabPtr;
        std::ostringstream  stream;
        ArrayFactory        f;


        //std::shared_ptr<TypedArray<std::complex<float>>> outmatrix;

        //TypedArray<std::complex<float>> outmatrix;
        struct args{
            char     op;
            uint32_t centerfreq;
            std::string ip,port,cport;
        }args;
		
//        


        //void initoutmtx(){
         //   outmatrix = f.createArray<std::complex<float>>({32768,2}, {o});
        //}
public:
       //friend class matlab::data::TypedArray<std::complex<float>>;
       

    MexFunction(): contextr(1),contextc(1),socketr(contextr,ZMQ_SUB),socketc(contextc,ZMQ_DEALER){
         mexLock();
         matlabPtr = getEngine();

         //initoutmtx();
    }
    ~MexFunction(){
    }


    void operator()(ArgumentList outputs, ArgumentList inputs) {

        TypedArray<std::complex<float>> outmatrix = f.createArray<std::complex<float>>({32768,22}, {o});
        //float* outptr = static_cast<float *>(outmatrix[0][0]);
        
        zmq::message_t message;
        const float scaler = 1.0f/128;

    	parseArguments(inputs);
        int N=22;
        int L=32768;
        
     	
     	switch (args.op) {
			case 'i':
		        initzmq(args.ip,args.port,args.cport);
            break;
			case 'r':{
                stream << "receiving..." << std::endl;
                displayOnMATLAB(stream);

                zmq::pollitem_t items[] = {
                    {(void *)socketr,0,ZMQ_POLLIN,0}
                };

                zmq::poll(&items[0],1, 100);
                if(items[0].revents & ZMQ_POLLIN){

                socketr.recv(&message,ZMQ_NOBLOCK);  //ZMQ_NOBLOCK seems to crash matlab. no message data, trying to access it anyhow crashes...
                int8_t *msgp =  static_cast<int8_t*>(message.data());
                uint32_t offset=16+sizeof(uint32_t)*N;
                int j=0;
                for (int l=0;l<L;++l)
                    for(int n=0;n<N;++n){
                        std::complex<float> out = scaler*std::complex<float>(msgp[n*L+l],msgp[n*L+l+1]);
                        outmatrix[l][n] = out;
                    }
                }
                }
			break;
			case 't':
                stream << "retuning to" << std::to_string(args.centerfreq) << std::endl;
                displayOnMATLAB(stream);
                retune(args.centerfreq);
			break;
			case 'c':
                mexUnlock();
			break;
			default:
			break;
		}

        outputs[0] = std::move(outmatrix); //f.createScalar<double>(101);
    }

    void retune(uint32_t fcenter){
        zmq::message_t message(sizeof(uint32_t)*3);
        uint32_t *msg =  static_cast<uint32_t*>(message.data());

        msg[0]=CONTROLMSG_MAGIC;
        msg[1]=CONTROLMSG_SETFCENTER;
        msg[2]=fcenter;
        
        socketc.send(message);
    }

    void initzmq(std::string ip, std::string p, std::string cp) { //, std::string port, std::string cport){
        
    	std::string addr = "tcp://" + ip + ":" + p;
        std::string caddr= "tcp://" + ip + ":" + cp;

        stream << "receiver address:" << addr << std::endl;
        displayOnMATLAB(stream);
        stream << "control address:" << caddr << std::endl;
        displayOnMATLAB(stream);
        
        socketr.connect(addr.data());
        socketr.setsockopt(ZMQ_SUBSCRIBE,"",0);

        socketc.connect(caddr.data());
        //subscriber.connect("tcp://localhost:5561");
        //subscriber.setsockopt(ZMQ_SUBSCRIBE, "", 0);
    }

    // read inputs into struct args, perform some checking against invalid input:
    void parseArguments(ArgumentList inputs){
        	
            int numargs = inputs.size();

            if (numargs>0){
                if(inputs[0].getType()!= ArrayType::CHAR)
        		  matlabPtr->feval(u"error",0, std::vector<Array>({f.createScalar("Incorrect input 0, operation char ('i','r','t','c').")}));
                const CharArray oper = inputs[0];
                args.op = oper[0];
            }
            if (numargs>1){
                if(inputs[1].getType()!= ArrayType::DOUBLE)
                    matlabPtr->feval(u"error",0, std::vector<Array>({f.createScalar("Incorrect input: Center frequency should be double type")}));
                TypedArray<double> cfreq = inputs[1];
                if (!cfreq.isEmpty())
                    args.centerfreq = cfreq[0];
            }
            if (numargs==5){
                if ((inputs[2].getType()!= ArrayType::CHAR)||(inputs[3].getType()!= ArrayType::CHAR)||(inputs[4].getType()!= ArrayType::CHAR))
                    matlabPtr->feval(u"error",0, std::vector<Array>({f.createScalar("Incorrect input: ip, port & control port should be char strings")}));
                
                args.ip    = std::string(((CharArray) inputs[2]).toAscii());
                args.port  = std::string(((CharArray) inputs[3]).toAscii());
                args.cport = std::string(((CharArray) inputs[4]).toAscii());
            }

    }

    //from matlab examples:
    void displayOnMATLAB(std::ostringstream& stream) {
        // Pass stream content to MATLAB fprintf function
        matlabPtr->feval(u"fprintf", 0,
            std::vector<Array>({ f.createScalar(stream.str()) }));
        // Clear stream buffer
        stream.str("");
    }
};