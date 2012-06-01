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
 m_close(false), m_started(false), m_frame(0), 
 is_mono(true), m_cameras(0), m_images_mono(0), m_images_rgb(0),
 m_coshell("hrp2010c", 2809)
{
}

SoTSequenceGrabber::~SoTSequenceGrabber() {
}

bool SoTSequenceGrabber::pre_fct() {

	string filename = get_sandbox() + string ( "/sot-sequence-grabber.conf") ;
	try {
		read_config_file( filename.c_str() ) ; 
	} catch ( string msg ) {
		
		cout << "[SoTSequenceGrabber] Could not read config file" << endl ;
		cout << "[SoTSequenceGrabber] Will grab all active cameras in MONO mode" << endl ;

	}

    vector<Camera*> cams = get_all_cameras() ;
    
    for (int i=0; i<cams.size(); i++ ) {
    	if ( cams[i]->is_active() ) {
            m_cameras.push_back(cams[i]);
    	}
    }

    for(size_t i = 0; i < m_cameras.size(); ++i)
    {
        path camera_path = path(get_sandbox()) / path(m_cameras[i]->get_name())  ;
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
        if(is_mono)
        {
    		register_to_cam< Image<unsigned char, vision::MONO> > ( m_cameras[i], 50 ) ;
        }
        else
        {
    		register_to_cam< Image<uint32_t, vision::RGB> > ( m_cameras[i], 50 ) ;
        }
    }
    try {
        XMLRPCServer * server = whiteboard_read<XMLRPCServer *>("plugin_xmlrpc-server");
        if(server)
        {
            std::cout << "[SoTSequenceGrabber] Found a XML-RPC server plugin" << std::endl;
            std::cout << "[SoTSequenceGrabber] Acquisition will start when requested via XML-RPC" << std::endl;
            server->AddMethod(this);
        }
    }
    catch(...)
    {
        std::cout << "[SoTSequenceGrabber] No XML-RPC server plugin registered to the server" << std::endl;
        std::cout << "[SoTSequenceGrabber] Will start acquisition right away !" << std::endl;
        if(is_mono)
        {
            m_save_th = new boost::thread(boost::bind(&SoTSequenceGrabber::save_images_loop_mono, this));
        }
        else
        {
            m_save_th = new boost::thread(boost::bind(&SoTSequenceGrabber::save_images_loop_rgb, this));
        }
    }
}

void SoTSequenceGrabber::loop_fct() {

    if(m_started)
    {
        for(unsigned int i = 0; i < m_cameras.size(); ++i)
        {
            if(is_mono)
            {
                vision::Image<unsigned char, vision::MONO> * img = dequeue_image< Image<unsigned char, vision::MONO> > (m_cameras[i]);

                m_images_mono.push_back(make_pair(m_cameras[i]->get_name(), new vision::Image<unsigned char, vision::MONO>(*img)));

                enqueue_image(m_cameras[i], img);
            }
            else
            {
                vision::Image<uint32_t, vision::RGB> * img = dequeue_image< Image<uint32_t, vision::RGB> > (m_cameras[i]);

                m_images_rgb.push_back(make_pair(m_cameras[i]->get_name(), new vision::Image<uint32_t, vision::RGB>(*img)));

                enqueue_image(m_cameras[i], img);
            }
        }
    }
    else
    {
        for(unsigned int i = 0; i < m_cameras.size(); ++i)
        {
            if(is_mono)
            {
                vision::Image<unsigned char, vision::MONO> * img = dequeue_image< Image<unsigned char, vision::MONO> > (m_cameras[i]);
                enqueue_image(m_cameras[i], img);
            }
            else
            {
                vision::Image<uint32_t, vision::RGB> * img = dequeue_image< Image<uint32_t, vision::RGB> > (m_cameras[i]);
                enqueue_image(m_cameras[i], img);
            }
        }
        m_frame++;
    }
}

bool SoTSequenceGrabber::post_fct() {
    for(unsigned int i = 0; i < m_cameras.size(); ++i)
    {
        if(is_mono)
        {
            unregister_to_cam< vision::Image<unsigned char, vision::MONO> >(m_cameras[i]);	
        }
        else
        {
            unregister_to_cam< vision::Image<uint32_t, vision::RGB> >(m_cameras[i]);	
        }
	}
    m_close = true;
    if(m_save_th)
    {
        m_save_th->join();
        delete m_save_th;
    } 
	return true ;
}


void SoTSequenceGrabber::parse_config_line( vector<string> &line ) 
{
    std::string mode;
    if( fill_member(line, "Mode", mode) )
    {
        is_mono = (mode == "MONO");
    }
}

void SoTSequenceGrabber::execute(XmlRpcValue & params, XmlRpcValue & result)
{
    std::string what(params[0]);
    if(what == "start" && !m_started)
    {
        if(is_mono)
        {
            m_save_th = new boost::thread(boost::bind(&SoTSequenceGrabber::save_images_loop_mono, this));
        }
        else
        {
            m_save_th = new boost::thread(boost::bind(&SoTSequenceGrabber::save_images_loop_rgb, this));
        }
    }
    if(what == "stop" && m_started)
    {
        m_close = true;
        m_save_th->join();
        delete m_save_th;
        m_save_th = 0;
    }
}

void SoTSequenceGrabber::save_images_loop_mono()
{
    {
        std::stringstream fname;
        fname << get_sandbox()  << "/sot_init-" << setfill('0') << setw(6) << m_frame;
        std::ofstream fs(fname.str().c_str());
        fs << m_coshell.ExecuteACommand("signalTime sot.control");
        fs.close();
    }
    m_started = true;
    while(!m_close)
    {
        while(!m_close && m_images_mono.size() < m_cameras.size() * (m_frame + 1))
        {
            usleep(1000);
        }
        unsigned int n = m_images_mono.size();
        for(size_t i = m_cameras.size() * m_frame; i < n; ++i)
        {
            std::stringstream filename;
            filename << get_sandbox()  << "/" << m_images_mono[i].first << "/" << setfill('0') << setw(6) << m_frame << ".bin";
            serialize(filename.str(), *(m_images_mono[i].second));
            delete m_images_mono[i].second;
            if(i % m_cameras.size() == m_cameras.size() - 1)
            {
                m_frame++;
            }
        }
    }
}

void SoTSequenceGrabber::save_images_loop_rgb()
{
    m_started = true;
    while(!m_close)
    {
        while(!m_close && m_images_rgb.size() < m_cameras.size() * (m_frame + 1))
        {
            usleep(1000);
        }
        unsigned int n = m_images_rgb.size();
        for(size_t i = m_cameras.size() * m_frame; i < n; ++i)
        {
            std::stringstream filename;
            filename << get_sandbox()  << "/" << m_images_rgb[i].first << "/" << setfill('0') << setw(6) << m_frame << ".bin";
            serialize(filename.str(), *(m_images_rgb[i].second));
            delete m_images_rgb[i].second;
            if(i % m_cameras.size() == m_cameras.size() - 1)
            {
                m_frame++;
            }
        }
    }
}

