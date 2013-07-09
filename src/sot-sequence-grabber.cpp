#include "sot-sequence-grabber.h"

#include <iomanip>
#include <vision/io/imageio.h>
#include <boost/filesystem.hpp>
#include <iostream>

#include <visionsystem/vs_plugins/xmlrpc/xmlrpc-server.h>

using namespace boost::filesystem ;

SoTSequenceGrabber::SoTSequenceGrabber( VisionSystem *vs, string sandbox )
:Plugin( vs, "sot-sequence-grabber", sandbox ), 
 XmlRpcServerMethod("SoTSequenceGrabber", 0),
 m_save_th_mono(0), m_save_th_rgb(0), m_save_th_depth(0),
 mono_count(false), rgb_count(false), depth_count(false),
 m_close(false), m_started(false),
 m_frame_mono(0), m_frame_rgb(0), m_frame_depth(0),
 m_cameras(0), m_images_mono(0), m_images_rgb(0), m_images_depth(0),
 m_coshell("hrp2010c", 2809)
{
    m_coshell.Initialize();
}

SoTSequenceGrabber::~SoTSequenceGrabber() {
}

bool SoTSequenceGrabber::pre_fct() {
    std::string filename = get_sandbox() + std::string ( "/sot-sequence-grabber.conf") ;
    try {
        read_config_file( filename.c_str() ) ;
    } catch ( std::string msg ) {

        std::cout << "[SoTSequenceGrabber] Could not read config file" << std::endl ;
        std::cout << "[SoTSequenceGrabber] Will grab all active cameras and guess best acquisition mode" << std::endl ;

    }

    /* Default case, grab all cameras */
    if(m_cameras.size() == 0)
    {
        std::vector<Camera*> cams = get_all_cameras() ;
        for(size_t i = 0; i < cams.size(); ++i)
        {
            m_cameras.push_back(CameraConfig(cams[i]->get_name(), ACQ_RGB, 0));
        }
    }


    for(std::vector<CameraConfig>::iterator it = m_cameras.begin(); it != m_cameras.end(); ++it)
    {
        Camera * cam = get_camera(it->name);
        if(cam && cam->is_active())
        {
            it->cam = cam;
            it->mode = fcoding_to_acq(cam->get_coding());
        }
        else
        {
            m_cameras.erase(it);
            --it;
        }
    }
    /* From now on we can assume that all Camera* in m_cameras are valid and refer to active cameras */

    for(size_t i = 0; i < m_cameras.size(); ++i)
    {
        path camera_path = path(get_sandbox()) / path(m_cameras[i].name)  ;
        if ( exists(camera_path) )
        {
            remove_all(camera_path);
        }
        create_directory(camera_path);
    }


    return true ;
}

void SoTSequenceGrabber::preloop_fct() {
    for ( unsigned int i=0; i< m_cameras.size(); ++i )
    {
        switch(m_cameras[i].mode)
        {
            case ACQ_MONO:
                mono_count++;
                register_to_cam< vision::Image<unsigned char, vision::MONO> > ( m_cameras[i].cam, 50 ) ;
                break;
            case ACQ_RGB:
                rgb_count++;
                register_to_cam< vision::Image<uint32_t, vision::RGB> > ( m_cameras[i].cam, 50 ) ;
                break;
            case ACQ_DEPTH:
                depth_count++;
                register_to_cam< vision::Image<uint16_t, vision::DEPTH> > ( m_cameras[i].cam, 50 ) ;
                break;
            default:
                break;
        }
    }
    try {
        XMLRPCServer * server = whiteboard_read<XMLRPCServer *>("plugin_xmlrpc-server");
        if(server)
        {
            server->AddMethod(this);
            std::cout << "[SoTSequenceGrabber] Found a XML-RPC server plugin" << std::endl;
            std::cout << "[SoTSequenceGrabber] Acquisition will start when requested via XML-RPC" << std::endl;
        }
    }
    catch(...)
    {
        std::cout << "[SoTSequenceGrabber] No XML-RPC server plugin registered to the server" << std::endl;
        std::cout << "[SoTSequenceGrabber] Will start acquisition right away !" << std::endl;
        if(mono_count)
        {
            m_save_th_mono = new boost::thread(boost::bind(&SoTSequenceGrabber::save_images_loop< vision::Image<unsigned char, vision::MONO> >,
                                                this, boost::cref(m_images_mono), mono_count, boost::ref(m_frame_mono)));
        }
        if(rgb_count)
        {
            m_save_th_rgb = new boost::thread(boost::bind(&SoTSequenceGrabber::save_images_loop< vision::Image<uint32_t, vision::RGB> >,
                                                this, boost::cref(m_images_rgb), rgb_count, boost::ref(m_frame_rgb)));
        }
        if(depth_count)
        {
            m_save_th_depth = new boost::thread(boost::bind(&SoTSequenceGrabber::save_images_loop< vision::Image<uint16_t, vision::DEPTH> >,
                                                this, boost::cref(m_images_depth), depth_count, boost::ref(m_frame_depth)));
        }
    }
}

void SoTSequenceGrabber::loop_fct() {
    if(m_started)
    {
		unsigned int sot_iter = 0;
		std::stringstream ss; ss << m_coshell.ExecuteACommand("signalTime sot.control");
		ss >> sot_iter;
		
        for(unsigned int i = 0; i < m_cameras.size(); ++i)
        {
            vision::Image<unsigned char, vision::MONO> * img_mono = 0;
            vision::Image<uint32_t, vision::RGB> * img_rgb = 0;
            vision::Image<uint16_t, vision::DEPTH> * img_depth = 0;
            switch(m_cameras[i].mode)
            {
                case ACQ_MONO:
                    img_mono = dequeue_image<vision::Image<unsigned char, vision::MONO> > (m_cameras[i].cam);
                    m_images_mono.push_back(std::make_pair(CamAndIter(m_cameras[i].name, sot_iter), new vision::Image<unsigned char, vision::MONO>(*img_mono)));
                    enqueue_image(m_cameras[i].cam, img_mono);
                    break;
                case ACQ_RGB:
                    img_rgb = dequeue_image<vision::Image<uint32_t, vision::RGB> > (m_cameras[i].cam);
                    m_images_rgb.push_back(std::make_pair(CamAndIter(m_cameras[i].name, sot_iter), new vision::Image<uint32_t, vision::RGB>(*img_rgb)));
                    enqueue_image(m_cameras[i].cam, img_rgb);
                    break;
                case ACQ_DEPTH:
                    img_depth = dequeue_image<vision::Image<uint16_t, vision::DEPTH> > (m_cameras[i].cam);
                    m_images_depth.push_back(std::make_pair(CamAndIter(m_cameras[i].name, sot_iter), new vision::Image<uint16_t, vision::DEPTH>(*img_depth)));
                    enqueue_image(m_cameras[i].cam, img_depth);
                    break;
                default:
                    break;
            }
        }
    }
    else
    {
        for(unsigned int i = 0; i < m_cameras.size(); ++i)
        {
            vision::Image<unsigned char, vision::MONO> * img_mono = 0;
            vision::Image<uint32_t, vision::RGB> * img_rgb = 0;
            vision::Image<uint16_t, vision::DEPTH> * img_depth = 0;
            switch(m_cameras[i].mode)
            {
                case ACQ_MONO:
                    img_mono = dequeue_image<vision::Image<unsigned char, vision::MONO> > (m_cameras[i].cam);
                    enqueue_image(m_cameras[i].cam, img_mono);
                    break;
                case ACQ_RGB:
                    img_rgb = dequeue_image<vision::Image<uint32_t, vision::RGB> > (m_cameras[i].cam);
                    enqueue_image(m_cameras[i].cam, img_rgb);
                    break;
                case ACQ_DEPTH:
                    img_depth = dequeue_image<vision::Image<uint16_t, vision::DEPTH> > (m_cameras[i].cam);
                    enqueue_image(m_cameras[i].cam, img_depth);
                    break;
                default:
                    break;
            }
        }
        m_frame_mono++;
        m_frame_rgb++;
        m_frame_depth++;
    }
}

bool SoTSequenceGrabber::post_fct() {
    for ( unsigned int i=0; i< m_cameras.size(); ++i )
    {
        switch(m_cameras[i].mode)
        {
            case ACQ_MONO:
                unregister_to_cam<vision::Image<unsigned char, vision::MONO> > ( m_cameras[i].cam ) ;
                break;
            case ACQ_RGB:
                unregister_to_cam<vision::Image<uint32_t, vision::RGB> > ( m_cameras[i].cam ) ;
                break;
            case ACQ_DEPTH:
                unregister_to_cam<vision::Image<uint16_t, vision::DEPTH> > ( m_cameras[i].cam ) ;
                break;
            default:
                break;
        }
    }
    m_close = true;
    if(m_save_th_mono)
    {
        m_save_th_mono->join();
        delete m_save_th_mono;
        m_save_th_mono = 0;
    }
    if(m_save_th_rgb)
    {
        m_save_th_rgb->join();
        delete m_save_th_rgb;
        m_save_th_rgb = 0;
    }
    if(m_save_th_depth)
    {
        m_save_th_depth->join();
        delete m_save_th_depth;
        m_save_th_depth = 0;
    }
    return true ;
}


void SoTSequenceGrabber::parse_config_line( vector<string> &line ) 
{
    std::vector<std::string> cams_in;
    if( fill_member(line, "Cameras", cams_in) )
    {
        for(size_t i = 0; i < cams_in.size(); ++i)
        {
            std::string cam_in = cams_in[i];
            CameraConfig conf;
            size_t sep = cam_in.rfind(':');
            if(sep != std::string::npos)
            {
                conf.name = cam_in.substr(0, sep);
                std::string mode = cam_in.substr(sep+1);
                if(mode == "MONO")
                {
                    conf.mode = ACQ_MONO;
                }
                else if(mode == "RGB")
                {
                    conf.mode = ACQ_RGB;
                }
                else if(mode == "DEPTH")
                {
                    conf.mode = ACQ_DEPTH;
                }
                else
                {
                    // default mode when the camera has ":" in its name
                    conf.name = cam_in;
                    conf.mode = ACQ_RGB;
                }
            }
            else
            {
                conf.name = cam_in;
                conf.mode = ACQ_RGB;
            }
            m_cameras.push_back(conf);
        }
    }
}

SoTSequenceGrabber::ACQUISITION_MODE SoTSequenceGrabber::fcoding_to_acq(const FrameCoding & coding)
{
    switch(coding)
    {
        case VS_MONO8:
        case VS_MONO16:
            return ACQ_MONO;
        case VS_DEPTH8:
        case VS_DEPTH16:
        case VS_DEPTH24:
        case VS_DEPTH32:
            return ACQ_DEPTH;
        /* Default cases (include RGB/YUV variations and unhandled like IR */
        default:
            return ACQ_RGB;
    }
}

void SoTSequenceGrabber::execute(XmlRpcValue & params, XmlRpcValue & result)
{
    std::string what(params[0]);
    if(what == "start" && !m_started)
    {
        m_close = false;
        if(mono_count)
        {
            m_save_th_mono = new boost::thread(boost::bind(&SoTSequenceGrabber::save_images_loop< vision::Image<unsigned char, vision::MONO> >,
                                                this, boost::cref(m_images_mono), mono_count, boost::ref(m_frame_mono)));
        }
        if(rgb_count)
        {
            m_save_th_rgb = new boost::thread(boost::bind(&SoTSequenceGrabber::save_images_loop< vision::Image<uint32_t, vision::RGB> >,
                                                this, boost::cref(m_images_rgb), rgb_count, boost::ref(m_frame_rgb)));
        }
        if(depth_count)
        {
            m_save_th_depth = new boost::thread(boost::bind(&SoTSequenceGrabber::save_images_loop< vision::Image<uint16_t, vision::DEPTH> >,
                                                this, boost::cref(m_images_depth), depth_count, boost::ref(m_frame_depth)));
        }
    }
    if(what == "stop" && m_started)
    {
        m_started = false;
        m_close = true;
        if(m_save_th_mono)
        {
            m_save_th_mono->join();
            delete m_save_th_mono;
            m_save_th_mono = 0;
        }
        if(m_save_th_rgb)
        {
            m_save_th_rgb->join();
            delete m_save_th_rgb;
            m_save_th_rgb = 0;
        }
        if(m_save_th_depth)
        {
            m_save_th_depth->join();
            delete m_save_th_depth;
            m_save_th_depth = 0;
        }
    }
}
