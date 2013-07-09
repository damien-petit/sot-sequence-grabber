#ifndef SOT_SEQUENCE_GRABBER_H
#define SOT_SEQUENCE_GRABBER_H

#include <configparser/configparser.h>

#include <vision/image/image.h>
#include <visionsystem/plugin.h>

#include <boost/thread.hpp>

#include <coshell-client/CoshellClient.h>

#include <XmlRpc.h>
#include <iomanip>

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

class SoTSequenceGrabber : public Plugin, public configparser::WithConfigFile, public XmlRpc::XmlRpcServerMethod
{

    public:

        SoTSequenceGrabber( VisionSystem *vs, std::string sandbox ) ;
        ~SoTSequenceGrabber() ;

        bool pre_fct()  ;
        void preloop_fct() ;
        void loop_fct() ;
        bool post_fct() ;

        /* Method for XML-RPC call */
        /* Params should be start/stop string */
        void execute(XmlRpc::XmlRpcValue & params, XmlRpc::XmlRpcValue & result);

    private:
        void parse_config_line( std::vector<std::string> &line ) ;

        boost::thread * m_save_th_mono;
        boost::thread * m_save_th_rgb;
        boost::thread * m_save_th_depth;
        unsigned int mono_count;
        unsigned int rgb_count;
        unsigned int depth_count;
        bool m_close;

        bool m_started;
        unsigned int m_frame_mono;
        unsigned int m_frame_rgb;
        unsigned int m_frame_depth;

        enum ACQUISITION_MODE
        {
            ACQ_MONO = 1,
            ACQ_RGB,
            ACQ_DEPTH
        };

        ACQUISITION_MODE fcoding_to_acq(const FrameCoding & coding);

        struct CameraConfig
        {
            std::string name;
            ACQUISITION_MODE mode;
            Camera * cam;
            CameraConfig(std::string s, ACQUISITION_MODE m, Camera * c)
            : name(s), mode(m), cam(c) {}
            CameraConfig() : name(""), mode(ACQ_MONO), cam(0) {}
            ~CameraConfig() {}
        };

        std::vector< CameraConfig > m_cameras;
        std::vector< std::pair< CamAndIter, vision::Image<unsigned char, vision::MONO> *> > m_images_mono;
        std::vector< std::pair< CamAndIter, vision::Image<uint32_t, vision::RGB> *> > m_images_rgb;
        std::vector< std::pair< CamAndIter, vision::Image<uint16_t, vision::DEPTH> *> > m_images_depth;

        template< typename TImage >
        void save_images_loop(const std::vector< std::pair< CamAndIter, TImage *> > & images,
            unsigned int camera_count, unsigned int & frame_count)
        {
            unsigned int treated_frames = images.size()/camera_count;
            m_started = true;
            while(!m_close)
            {
                while(!m_close && images.size() < camera_count * (treated_frames + 1))
                {
                    usleep(1000);
                }
                unsigned int n = images.size();
                for(size_t i = camera_count * treated_frames; i < n; ++i)
                {
                    std::stringstream filename;
                    filename << get_sandbox()  << "/" << images[i].first.cam_name << "/" << std::setfill('0') << std::setw(10) << images[i].first.sot_iter << ".bin";
                    serialize(filename.str(), *(images[i].second));
                    delete images[i].second;
                    if(i % camera_count == camera_count - 1)
                    {
                        frame_count++;
                        treated_frames++;
                    }
                }
            }
        }
		
		coshell::CoshellClient m_coshell;
} ;

PLUGIN( SoTSequenceGrabber ) 

#endif
