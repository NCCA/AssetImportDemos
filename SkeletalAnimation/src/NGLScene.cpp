#include <QMouseEvent>
#include <QGuiApplication>

#include "NGLScene.h"
#include "AIUtil.h"
#include <iostream>
#include <ngl/NGLInit.h>
#include <ngl/NGLStream.h>
#include <ngl/ShaderLib.h>
#include <ngl/NGLStream.h>
#include <assimp/cimport.h>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <assimp/vector3.h>
#include <QTime>
#include "MultiBufferIndexVAO.h"

NGLScene::NGLScene(const char *_fname)
{
  setTitle("Using libassimp with NGL for Animation");
  m_animate = true;
  m_frameTime = 0.0f;
  m_sceneName = _fname;
}

NGLScene::~NGLScene()
{
  std::cout << "Shutting down NGL, removing VAO's and Shaders\n";
}

void NGLScene::resizeGL(int _w, int _h)
{
  m_project = ngl::perspective(45.0f, static_cast<float>(_w) / _h, 0.05f, 350.0f);
  m_win.width = static_cast<int>(_w * devicePixelRatio());
  m_win.height = static_cast<int>(_h * devicePixelRatio());
}
void NGLScene::initializeGL()
{
  // we must call this first before any other GL commands to load and link the
  // gl commands from the lib, if this is not done program will crash
  ngl::NGLInit::initialize();
  // register our new Factory to draw the VAO
  ngl::VAOFactory::registerVAOCreator("multiBufferIndexVAO", MultiBufferIndexVAO::create);
  ngl::VAOFactory::listCreators();

  glClearColor(0.4f, 0.4f, 0.4f, 1.0f); // Grey Background
  // enable depth testing for drawing
  glEnable(GL_DEPTH_TEST);
  // enable multisampling for smoother drawing
  glEnable(GL_MULTISAMPLE);
  m_scene = m_importer.ReadFile(m_sceneName,
                                aiProcessPreset_TargetRealtime_Quality |
                                    aiProcess_Triangulate);
  if (m_scene == nullptr)
  {
    std::cerr << "Error loading scene file\n";
    exit(EXIT_FAILURE);
  }
  std::cout << "num animations " << m_scene->mNumAnimations << "\n";
  m_numAnimations = m_scene->mNumAnimations;
  if (m_scene->mNumAnimations < 1)
  {
    std::cerr << "No animations in this scene exiting\n";
    exit(EXIT_FAILURE);
  }
  bool loaded = m_mesh.load(m_scene);
  if (loaded == false)
  {
    std::cerr << "Assimp reports " << m_importer.GetErrorString() << "\n";
    exit(EXIT_FAILURE);
  }
  // now to load the shader and set the values
  // we are creating a shader called Skinning use string to avoid typos
  auto constexpr Skinning = "Skinning";
  auto constexpr SkinningVertex = "SkinningVertex";
  auto constexpr SkinningFragment = "SkinningFragment";

  ngl::ShaderLib::createShaderProgram(Skinning);
  // now we are going to create empty shaders for Frag and Vert
  ngl::ShaderLib::attachShader(SkinningVertex, ngl::ShaderType::VERTEX);
  ngl::ShaderLib::attachShader(SkinningFragment, ngl::ShaderType::FRAGMENT);
  // attach the source
  ngl::ShaderLib::loadShaderSource(SkinningVertex, "shaders/SkinningVertex.glsl");
  ngl::ShaderLib::loadShaderSource(SkinningFragment, "shaders/SkinningFragment.glsl");
  // compile the shaders
  ngl::ShaderLib::compileShader(SkinningVertex);
  ngl::ShaderLib::compileShader(SkinningFragment);
  // add them to the program
  ngl::ShaderLib::attachShaderToProgram(Skinning, SkinningVertex);
  ngl::ShaderLib::attachShaderToProgram(Skinning, SkinningFragment);
  // now bind the shader attributes for most NGL primitives we use the following
  // now we have associated this data we can link the shader
  ngl::ShaderLib::linkProgramObject(Skinning);
  ngl::ShaderLib::printRegisteredUniforms(Skinning);
  ngl::ShaderLib::use(Skinning);

  ngl::Vec3 min, max;
  AIU::getSceneBoundingBox(m_scene, min, max);
  ngl::Vec3 center = (min + max) / 2.0f;
  ngl::Vec3 from;
  from.m_x = 0;
  from.m_y = max.m_y * 4.0f;
  from.m_z = max.m_z * 4.0f;
  std::cout << "from " << from << " center " << center << "\n";
  // now load to our new camera
  m_view = ngl::lookAt(from, center, ngl::Vec3::up());
  // set the shape using FOV 45 Aspect Ratio based on Width and Height
  // The final two are near and far clipping planes of 0.5 and 10
  m_project = ngl::perspective(45.0f, 720.0f / 576.0f, 0.05f, 350.0f);

  ngl::Vec4 lightPos = from;
  ngl::Mat4 iv = m_view;
  iv.inverse().transpose();
  ngl::ShaderLib::setUniform("light.position", lightPos * iv);
  ngl::ShaderLib::setUniform("light.ambient", 0.1f, 0.1f, 0.1f, 1.0f);
  ngl::ShaderLib::setUniform("light.diffuse", 1.0f, 1.0f, 1.0f, 1.0f);
  ngl::ShaderLib::setUniform("light.specular", 0.8f, 0.8f, 0.8f, 1.0f);
  // gold like phong material
  ngl::ShaderLib::setUniform("material.ambient", 0.274725f, 0.1995f, 0.0745f, 0.0f);
  ngl::ShaderLib::setUniform("material.diffuse", 0.75164f, 0.60648f, 0.22648f, 0.0f);
  ngl::ShaderLib::setUniform("material.specular", 0.628281f, 0.555802f, 0.3666065f, 0.0f);
  ngl::ShaderLib::setUniform("material.shininess", 51.2f);
  ngl::ShaderLib::setUniform("viewerPos", from);

  // now create our light this is done after the camera so we can pass the
  // transpose of the projection matrix to the light to do correct eye space
  // transformations
  startTimer(20);
}

void NGLScene::loadMatricesToShader()
{
  ngl::Mat4 MV;
  ngl::Mat4 MVP;
  ngl::Mat4 M;
  M = m_mouseGlobalTX * m_transform.getMatrix();
  MV = m_view * M;
  MVP = m_project * MV;
  ngl::ShaderLib::setUniform("MV", MV);
  ngl::ShaderLib::setUniform("MVP", MVP);
  ngl::ShaderLib::setUniform("M", M);
}

void NGLScene::paintGL()
{
  glViewport(0, 0, m_win.width, m_win.height);
  // clear the screen and depth buffer
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  ngl::ShaderLib::use("Skinning");

  // Rotation based on the mouse position for our global transform
  ngl::Transformation trans;
  auto rotX = ngl::Mat4::rotateX(m_win.spinXFace);
  auto rotY = ngl::Mat4::rotateY(m_win.spinYFace);
  // multiply the rotations
  m_mouseGlobalTX = rotY * rotX;
  // add the translations
  m_mouseGlobalTX.m_m[3][0] = m_modelPos.m_x;
  m_mouseGlobalTX.m_m[3][1] = m_modelPos.m_y;
  m_mouseGlobalTX.m_m[3][2] = m_modelPos.m_z;
  // set this in the TX stack
  loadMatricesToShader();
  std::vector<ngl::Mat4> transforms;
  if (m_animate)
  {
    QTime t = QTime::currentTime();
    float time = float(t.msec()) / 1000.0f * float(m_mesh.getDuration() / m_mesh.getTicksPerSec());
    m_mesh.boneTransform(time, transforms);
  }
  else
  {
    m_mesh.boneTransform(m_frameTime, transforms);
  }

  auto size = transforms.size();
  for (unsigned int i = 0; i < size; ++i)
  {
    std::string name = fmt::format("gBones[{0}]", i);
    ngl::ShaderLib::setUniform(name, transforms[i]);
  }

  m_mesh.render();
}

//----------------------------------------------------------------------------------------------------------------------

void NGLScene::keyPressEvent(QKeyEvent *_event)
{
  // this method is called every time the main window recives a key event.
  // we then switch on the key value and set the camera in the GLWindow
  switch (_event->key())
  {
  // escape key to quite
  case Qt::Key_Escape:
    QGuiApplication::exit(EXIT_SUCCESS);
    break;
  // turn on wirframe rendering
  case Qt::Key_W:
    glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
    break;
  // turn off wire frame
  case Qt::Key_S:
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    break;
  // show full screen
  case Qt::Key_F:
    showFullScreen();
    break;
  // show windowed
  case Qt::Key_N:
    showNormal();
    break;
  case Qt::Key_Left:
    --m_activeAnimation;
    m_activeAnimation = std::clamp(m_activeAnimation, size_t(0), m_numAnimations - 1);
    m_mesh.setActiveAnimation(m_activeAnimation);

    break;
  case Qt::Key_Right:
    ++m_activeAnimation;
    m_activeAnimation = std::clamp(m_activeAnimation, size_t(0), m_numAnimations - 1);
    m_mesh.setActiveAnimation(m_activeAnimation);
    break;

  default:
    break;
  }
  update();
}

void NGLScene::timerEvent(QTimerEvent *)
{
  update();
}
