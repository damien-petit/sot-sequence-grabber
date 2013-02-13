#ifndef SEQUENCE_GRABBER_H
#define SEQUENCE_GRABBER_H

#include <configparser/configparser.h>

#include <vision/image/image.h>
#include <visionsystem/plugin.h>

#include <boost/thread.hpp>

#include <coshell-bci/CoshellClient.h>

#include <XmlRpc.h>

using namespace std;
using namespace vision;
using namespace visionsystem;
using namespace configparser;
using namespace XmlRpc;

struct CamAndIter
{
    CamAndIter() : cam_name("undefined"), sot_iter(0) 
    {}
    CamAndIter(const std::string & s, unsigned int i)
    : cam_name(s), sot_iter(i) 
    {}
    std::string cam_name;
    unsigned int sot_iter;
};

class SoTSequenceGrabber : public Plugin, public WithConfigFile, public XmlRpcServerMethod 
{

	public:
		
		SoTSequenceGrabber( VisionSystem *vs, string sandbox ) ;
		~SoTSequenceGrabber() ;

		bool pre_fct()  ;
		void preloop_fct() ;
		void loop_fct() ;
		bool post_fct() ;

        /* Method for XML-RPC call */
        /* Params should be start/stop string */
        void execute(XmlRpcValue & params, XmlRpcValue & result); 

	private:
		void parse_config_line( vector<string> &line ) ;

        void save_images_loop_mono();
        void save_images_loop_rgb();

        boost::thread * m_save_th; 
        bool m_close;

        bool m_started;
        unsigned int m_frame;

        bool is_mono;

        std::vector< Camera * > m_cameras;
        std::vector< std::pair< CamAndIter, vision::Image<unsigned char, vision::MONO> *> > m_images_mono; 
        std::vector< std::pair< CamAndIter, vision::Image<uint32_t, vision::RGB> *> > m_images_rgb; 

        coshellbci::CoshellClient m_coshell;
} ;



PLUGIN( SoTSequenceGrabber ) 

#endif
