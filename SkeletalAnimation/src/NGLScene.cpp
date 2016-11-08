#include <QMouseEvent>
#include <QGuiApplication>

#include "NGLScene.h"
#include "AIUtil.h"
#include <iostream>
#include <ngl/NGLInit.h>
#include <ngl/NGLStream.h>
#include <ngl/ShaderLib.h>
#include <ngl/Material.h>
#include <ngl/NGLStream.h>
#include <assimp/cimport.h>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <assimp/vector3.h>
#include <QTime>
#include <boost/format.hpp>



NGLScene::NGLScene(const char *_fname)
{
  setTitle("Using libassimp with NGL for Animation");
  m_animate=true;
  m_frameTime=0.0f;
  m_sceneName=_fname;
}


NGLScene::~NGLScene()
{
  std::cout<<"Shutting down NGL, removing VAO's and Shaders\n";
}



void NGLScene::resizeGL(int _w , int _h)
{
  m_cam.setShape(45.0f,static_cast<float>(_w)/_h,0.05f,350.0f);
  m_win.width=static_cast<int>(_w*devicePixelRatio());
  m_win.height=static_cast<int>(_h*devicePixelRatio());
}
void NGLScene::initializeGL()
{
  // we must call this first before any other GL commands to load and link the
  // gl commands from the lib, if this is not done program will crash
  ngl::NGLInit::instance();

  glClearColor(0.4f, 0.4f, 0.4f, 1.0f);			   // Grey Background
  // enable depth testing for drawing
  glEnable(GL_DEPTH_TEST);
  // enable multisampling for smoother drawing
  glEnable(GL_MULTISAMPLE);
  m_scene = m_importer.ReadFile(m_sceneName,
                                aiProcessPreset_TargetRealtime_Quality |
                                aiProcess_Triangulate
                                );
  if(m_scene == 0)
  {
    std::cerr<<"Error loading scene file\n";
    exit(EXIT_FAILURE);
  }
  std::cout<<"num animations "<<m_scene->mNumAnimations<<"\n";
  if(m_scene->mNumAnimations <1)
  {
      std::cerr<<"No animations in this scene exiting\n";
      exit(EXIT_FAILURE);
  }
  bool loaded=m_mesh.load(m_scene);
  if(loaded == false)
  {
      std::cerr<<"Assimp reports "<<m_importer.GetErrorString()<<"\n";
      exit(EXIT_FAILURE);
  }
  // now to load the shader and set the values
  // grab an instance of shader manager
  ngl::ShaderLib *shader=ngl::ShaderLib::instance();
  // we are creating a shader called Skinning use string to avoid typos
  auto constexpr Skinning="Skinning";
  auto constexpr SkinningVertex="SkinningVertex";
  auto constexpr SkinningFragment="SkinningFragment";

  shader->createShaderProgram(Skinning);
  // now we are going to create empty shaders for Frag and Vert
  shader->attachShader(SkinningVertex,ngl::ShaderType::VERTEX);
  shader->attachShader(SkinningFragment,ngl::ShaderType::FRAGMENT);
  // attach the source
  shader->loadShaderSource(SkinningVertex,"shaders/SkinningVertex.glsl");
  shader->loadShaderSource(SkinningFragment,"shaders/SkinningFragment.glsl");
  // compile the shaders
  shader->compileShader(SkinningVertex);
  shader->compileShader(SkinningFragment);
  // add them to the program
  shader->attachShaderToProgram(Skinning,SkinningVertex);
  shader->attachShaderToProgram(Skinning,SkinningFragment);
  // now bind the shader attributes for most NGL primitives we use the following
  // now we have associated this data we can link the shader
  shader->linkProgramObject(Skinning);
  shader->printRegisteredUniforms(Skinning);
  (*shader)[Skinning]->use();

  // the shader will use the currently active material and light0 so set them
  ngl::Material m(ngl::STDMAT::GOLD);
  // load our material values to the shader into the structure material (see Vertex shader)
  m.loadToShader("material");
  ngl::Vec3 min,max;
  AIU::getSceneBoundingBox(m_scene,min,max);
  ngl::Vec3 center=(min+max)/2.0f;
  ngl::Vec3 from;
  from.m_x=0;
  from.m_y=max.m_y*4.0f;
  from.m_z=max.m_z*4.0f;
  std::cout<<"from "<<from<<" center "<<center<<"\n";
  // now load to our new camera
  m_cam.set(from,center,ngl::Vec3::up());
  // set the shape using FOV 45 Aspect Ratio based on Width and Height
  // The final two are near and far clipping planes of 0.5 and 10
  m_cam.setShape(45.0f,720.0f/576.0f,0.05f,350.0f);
  shader->setUniform("viewerPos",m_cam.getEye().toVec3());
  // now create our light this is done after the camera so we can pass the
  // transpose of the projection matrix to the light to do correct eye space
  // transformations
  ngl::Mat4 iv=m_cam.getViewMatrix();
  iv.transpose();
  ngl::Light light(from,ngl::Colour(1,1,1,1),ngl::Colour(1,1,1,1),ngl::LightModes::POINTLIGHT );
  light.setTransform(iv);
  // load these values to the shader as well
  light.loadToShader("light");
  startTimer(20);
}




void NGLScene::loadMatricesToShader()
{
  ngl::ShaderLib *shader=ngl::ShaderLib::instance();

    ngl::Mat4 MV;
    ngl::Mat4 MVP;
    ngl::Mat4 M;
    M=m_transform.getMatrix()*m_mouseGlobalTX;
    MV=  M*m_cam.getViewMatrix();
    MVP= M*m_cam.getVPMatrix();
    shader->setUniform("MV",MV);
    shader->setUniform("MVP",MVP);
    shader->setUniform("M",M);
}

void NGLScene::paintGL()
{
  glViewport(0,0,m_win.width,m_win.height);
  // clear the screen and depth buffer
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  // grab an instance of the shader manager
  ngl::ShaderLib *shader=ngl::ShaderLib::instance();
  (*shader)["Skinning"]->use();

  // Rotation based on the mouse position for our global transform
  ngl::Transformation trans;
  ngl::Mat4 rotX;
  ngl::Mat4 rotY;
  // create the rotation matrices
  rotX.rotateX(m_win.spinXFace);
  rotY.rotateY(m_win.spinYFace);
  // multiply the rotations
  m_mouseGlobalTX=rotY*rotX;
  // add the translations
  m_mouseGlobalTX.m_m[3][0] = m_modelPos.m_x;
  m_mouseGlobalTX.m_m[3][1] = m_modelPos.m_y;
  m_mouseGlobalTX.m_m[3][2] = m_modelPos.m_z;
  // set this in the TX stack
  loadMatricesToShader();
  std::vector<ngl::Mat4> transforms;
  if(m_animate)
  {
    QTime t=QTime::currentTime();
    float time=(t.msec()/1000.0f)*m_mesh.getDuration()/m_mesh.getTicksPerSec();
    m_mesh.boneTransform(time, transforms);
  }
  else
  {
    m_mesh.boneTransform(m_frameTime, transforms);
  }

  auto size=transforms.size();
  for (unsigned int i = 0 ; i < size ; ++i)
  {
    std::string name=boost::str(boost::format("gBones[%d]") % i );
    shader->setUniform(name.c_str(),transforms[i]);
  }



  m_mesh.render();
}

//----------------------------------------------------------------------------------------------------------------------
void NGLScene::mouseMoveEvent( QMouseEvent* _event )
{
  // note the method buttons() is the button state when event was called
  // that is different from button() which is used to check which button was
  // pressed when the mousePress/Release event is generated
  if ( m_win.rotate && _event->buttons() == Qt::LeftButton )
  {
    int diffx = _event->x() - m_win.origX;
    int diffy = _event->y() - m_win.origY;
    m_win.spinXFace += static_cast<int>( 0.5f * diffy );
    m_win.spinYFace += static_cast<int>( 0.5f * diffx );
    m_win.origX = _event->x();
    m_win.origY = _event->y();
    update();
  }
  // right mouse translate code
  else if ( m_win.translate && _event->buttons() == Qt::RightButton )
  {
    int diffX      = static_cast<int>( _event->x() - m_win.origXPos );
    int diffY      = static_cast<int>( _event->y() - m_win.origYPos );
    m_win.origXPos = _event->x();
    m_win.origYPos = _event->y();
    m_modelPos.m_x += INCREMENT * diffX;
    m_modelPos.m_y -= INCREMENT * diffY;
    update();
  }
}


//----------------------------------------------------------------------------------------------------------------------
void NGLScene::mousePressEvent( QMouseEvent* _event )
{
  // that method is called when the mouse button is pressed in this case we
  // store the value where the maouse was clicked (x,y) and set the Rotate flag to true
  if ( _event->button() == Qt::LeftButton )
  {
    m_win.origX  = _event->x();
    m_win.origY  = _event->y();
    m_win.rotate = true;
  }
  // right mouse translate mode
  else if ( _event->button() == Qt::RightButton )
  {
    m_win.origXPos  = _event->x();
    m_win.origYPos  = _event->y();
    m_win.translate = true;
  }
}

//----------------------------------------------------------------------------------------------------------------------
void NGLScene::mouseReleaseEvent( QMouseEvent* _event )
{
  // that event is called when the mouse button is released
  // we then set Rotate to false
  if ( _event->button() == Qt::LeftButton )
  {
    m_win.rotate = false;
  }
  // right mouse translate mode
  if ( _event->button() == Qt::RightButton )
  {
    m_win.translate = false;
  }
}

//----------------------------------------------------------------------------------------------------------------------
void NGLScene::wheelEvent( QWheelEvent* _event )
{

  // check the diff of the wheel position (0 means no change)
  if ( _event->delta() > 0 )
  {
    m_modelPos.m_z += ZOOM;
  }
  else if ( _event->delta() < 0 )
  {
    m_modelPos.m_z -= ZOOM;
  }
  update();
}
//----------------------------------------------------------------------------------------------------------------------

void NGLScene::keyPressEvent(QKeyEvent *_event)
{
  // this method is called every time the main window recives a key event.
  // we then switch on the key value and set the camera in the GLWindow
  switch (_event->key())
  {
  // escape key to quite
  case Qt::Key_Escape : QGuiApplication::exit(EXIT_SUCCESS); break;
  // turn on wirframe rendering
  case Qt::Key_W : glPolygonMode(GL_FRONT_AND_BACK,GL_LINE); break;
  // turn off wire frame
  case Qt::Key_S : glPolygonMode(GL_FRONT_AND_BACK,GL_FILL); break;
  // show full screen
  case Qt::Key_F : showFullScreen(); break;
  // show windowed
  case Qt::Key_N : showNormal(); break;
  default : break;
  }
  // finally update the GLWindow and re-draw
  //if (isExposed())
    update();
}

void NGLScene::timerEvent(QTimerEvent *)
{
  update();
}
