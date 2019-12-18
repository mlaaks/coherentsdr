OBSOLETE, new project: mlaaks/coherent-rtlsdr

On the hardware side, you'll need all your connected RTL-SDR devices running of a single common clock. Then, you'd be better off with
having a distributed noise source, with one reference sdr-dongle recording only the noise signal. Otherwise, you cannot use
this software for it's intended use. The synchronization is achieved by computing the cross-correlation of each signal dongle vs.
reference dongle data (fft-xcorr, hence we require fftw). In our system, the noise is distributed to all dongles via mutual induction,
i.e no galvanic connection.

Developement is still slightly incomplete and the code is ugly. The samples are published to a ZMQ-socket, currently tcp://*:5555.
The actual zmq-messages contain a short header before the raw sample data starts. The length of this header depends on the number
of dongles initialized. I intend to add a commandline argument to exclude this header when I have the time.

The format is interleaved signed byte for the complex valued I&Q -data. Visualizing the quality of synchronization is difficult
from commandline, but there is an attempt to use VT100 -escape sequences to print out non-moving lag/lead data.


On the software side, following libraries are required before successfull build:
	
	:zmq: - the zero message queue
		sudo apt-get install libzmq3-dev
	
	:fftw3f: - fastest fourier transform in the west
		sudo apt-get install fftw3-dev
	
	:volk: - vector optimized library of kernels
		sudo apt-get install volk
	
	:Experimental rtl-sdr fork: - this needs to be built from source:
	Get libusb developer libraries (needs root or sudo):
		sudo apt-get install libusb-1.0-0-dev
	Download the experimental driver source:
		git clone git://github.com/tejeez/rtl-sdr
		cd rtl-sdr
		mkdir build
		cmake .. -DDETACH_KERNEL_DRIVER=ON -DINSTALL_UDEV_RULES=ON
		make -j 4
		sudo make install
		
		sudo ldconfig
		sudo udevadm control -R
		sudo udevadm trigger

Once the above prequisites are met, building the executable follows the normal cmake procedure, i.e:
	mkdir build
	cd build
	cmake ..
	build -j4

output from ./coherentsdr -h:

coherentsdr - synchronous rtl-sdr reader, IQ-samples published to a ZMQ-socket (atm, this is tcp://*:5555)
Dongles need to be clocked from the same signal, otherwise cross-correlation and synchronization will result in garbage

Usage:
        [-f frequency_to_tune_to [Hz'(default 480MHz)]
        [-s samplerate (default: 2048000 Hz)]
        [-n number of devices to init
        [-S use synchronous read (default: async) NOT FUNCTIONAL CURRENTLY]
        [-g tuner gain: signal [dB] (default 60)]
        [-r tuner gain: reference [dB] (default 50)]
        [-I reference dongle serial ID (default 'MREF')]
        [-A set automatic gaincontrol for all devices]
