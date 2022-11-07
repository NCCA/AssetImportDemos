#include <QMouseEvent>
#include <QGuiApplication>

#include "NGLScene.h"
#include "AIUtil.h"
#include <iostream>
#include <ngl/NGLInit.h>
#include <ngl/NGLStream.h>
#include <ngl/ShaderLib.h>
#include <ngl/VAOFactory.h>
#include <ngl/SimpleVAO.h>
#include <assimp/cimport.h>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <assimp/vector3.h>

NGLScene::NGLScene(const std::string &_fname)
{
  setTitle("Using libassimp with NGL simple Mesh");
  // the following code is modified from this
  // http://assimp.svn.sourceforge.net/viewvc/assimp/trunk/samples/SimpleOpenGL/
  // we are taking one of the postprocessing presets to avoid
  // spelling out 20+ single postprocessing flags here.

  m_scene = aiImportFile(_fname.c_str(),
                         aiProcessPreset_TargetRealtime_MaxQuality |
                             aiProcess_Triangulate |
                             aiProcess_PreTransformVertices |
                             aiProcess_FixInfacingNormals);
  if (m_scene == nullptr)
  {
    std::cout << "error opening file " << _fname << "\n";
    exit(EXIT_FAILURE);
  }
}

NGLScene::~NGLScene()
{
  std::cout << "Shutting down NGL, removing VAO's and Shaders\n";
}

void NGLScene::resizeGL(int _w, int _h)
{
  m_project = ngl::perspective(45.0f, static_cast<float>(_w) / _h, 0.5f, 550.0f);
  m_win.width = static_cast<int>(_w * devicePixelRatio());
  m_win.height = static_cast<int>(_h * devicePixelRatio());
}
void NGLScene::initializeGL()
{
  // we must call this first before any other GL commands to load and link the
  // gl commands from the lib, if this is not done program will crash
  ngl::NGLInit::initialize();

  glClearColor(0.4f, 0.4f, 0.4f, 1.0f); // Grey Background
  // enable depth testing for drawing
  glEnable(GL_DEPTH_TEST);
  // enable multisampling for smoother drawing
  glEnable(GL_MULTISAMPLE);
  // Now we will create a basic Camera from the graphics library
  // This is a static camera so it only needs to be set once
  // First create Values for the camera position
  ngl::Vec3 min, max;
  AIU::getSceneBoundingBox(m_scene, min, max);
  ngl::Vec3 center = (min + max) / 2.0f;
  ngl::Vec3 from;
  from.m_x = 0.0f;
  from.m_y = max.m_y * 4.0f;
  from.m_z = max.m_z * 4.0f;
  std::cout << "from " << from << " center " << center << "\n";

  // now load to our new camera
  m_view = ngl::lookAt(from, center, ngl::Vec3::up());

  // now to load the shader and set the values
  // we are creating a shader called Phong to save typos
  // in the code create some constexpr
  constexpr auto shaderProgram = "Phong";
  constexpr auto vertexShader = "PhongVertex";
  constexpr auto fragShader = "PhongFragment";
  // create the shader program
  ngl::ShaderLib::createShaderProgram(shaderProgram);
  // now we are going to create empty shaders for Frag and Vert
  ngl::ShaderLib::attachShader(vertexShader, ngl::ShaderType::VERTEX);
  ngl::ShaderLib::attachShader(fragShader, ngl::ShaderType::FRAGMENT);
  // attach the source
  ngl::ShaderLib::loadShaderSource(vertexShader, "shaders/PhongVertex.glsl");
  ngl::ShaderLib::loadShaderSource(fragShader, "shaders/PhongFragment.glsl");
  // compile the shaders
  ngl::ShaderLib::compileShader(vertexShader);
  ngl::ShaderLib::compileShader(fragShader);
  // add them to the program
  ngl::ShaderLib::attachShaderToProgram(shaderProgram, vertexShader);
  ngl::ShaderLib::attachShaderToProgram(shaderProgram, fragShader);

  // now we have associated that data we can link the shader
  ngl::ShaderLib::linkProgramObject(shaderProgram);
  // and make it active ready to load values
  ngl::ShaderLib::use(shaderProgram);
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

  buildVAOFromScene();
  // as re-size is not explicitly called we need to do this.
  m_project = ngl::perspective(45.0f, static_cast<float>(width()) / height(), 0.5f, 550.0f);
}

void NGLScene::buildVAOFromScene()
{

  recurseScene(m_scene, m_scene->mRootNode, ngl::Mat4(1.0));
}

// a simple structure to hold our vertex data
struct vertData
{
  GLfloat x;
  GLfloat y;
  GLfloat z;
  GLfloat nx;
  GLfloat ny;
  GLfloat nz;
  GLfloat u;
  GLfloat v;
};

void NGLScene::recurseScene(const aiScene *sc, const aiNode *nd, const ngl::Mat4 &_parentTx)
{
  ngl::Mat4 m = AIU::aiMatrix4x4ToNGLMat4Transpose(nd->mTransformation);

  unsigned int n = 0, t;
  std::vector<vertData> verts;
  vertData v;
  meshItem thisMesh;
  // the transform is relative to the parent node so we accumulate
  thisMesh.tx = m * _parentTx;

  for (; n < nd->mNumMeshes; ++n)
  {
    verts.clear();
    const aiMesh *mesh = m_scene->mMeshes[nd->mMeshes[n]];

    thisMesh.vao = ngl::VAOFactory::createVAO(ngl::simpleVAO, GL_TRIANGLES);
    for (t = 0; t < mesh->mNumFaces; ++t)
    {
      const aiFace *face = &mesh->mFaces[t];
      // only deal with triangles for ease
      if (face->mNumIndices != 3)
      {
        std::cout << "mesh size not tri" << face->mNumIndices << "\n";
        break;
      }
      for (unsigned int i = 0; i < face->mNumIndices; i++)
      {
        unsigned int index = face->mIndices[i];

        if (mesh->mNormals != nullptr)
        {
          v.nx = mesh->mNormals[index].x;
          v.ny = mesh->mNormals[index].y;
          v.nz = mesh->mNormals[index].z;
        }
        if (mesh->HasTextureCoords(0))
        {
          v.u = mesh->mTextureCoords[0]->x;
          v.v = mesh->mTextureCoords[0]->y;
        }
        v.x = mesh->mVertices[index].x;
        v.y = mesh->mVertices[index].y;
        v.z = mesh->mVertices[index].z;
        verts.push_back(v);
      }
    }
    thisMesh.vao->bind();
    // now we have our data add it to the VAO, we need to tell the VAO the following
    // how much (in bytes) data we are copying
    // a pointer to the first element of data (in this case the address of the first element of the
    // std::vector
    thisMesh.vao->setData(ngl::AbstractVAO::VertexData(verts.size() * sizeof(vertData), verts[0].x));
    // in this case we have packed our data in interleaved format as follows
    // u,v,nx,ny,nz,x,y,z
    // If you look at the shader we have the following attributes being used
    // attribute vec3 inVert; attribute 0
    // attribute vec3 inNormal; attribure 1
    // attribute vec2 inUV; attribute 2
    thisMesh.vao->setVertexAttributePointer(0, 3, GL_FLOAT, sizeof(vertData), 0);
    thisMesh.vao->setVertexAttributePointer(1, 3, GL_FLOAT, sizeof(vertData), 3);
    thisMesh.vao->setVertexAttributePointer(2, 2, GL_FLOAT, sizeof(vertData), 6);
    thisMesh.vao->setNumIndices(verts.size());
    // finally we have finished for now so time to unbind the VAO
    thisMesh.vao->unbind();
    m_meshes.emplace_back(std::move(thisMesh));
  }

  // draw all children
  for (n = 0; n < nd->mNumChildren; ++n)
  {
    recurseScene(sc, nd->mChildren[n], thisMesh.tx);
  }
}

void NGLScene::loadMatricesToShader()
{

  ngl::Mat4 MV;
  ngl::Mat4 MVP;
  ngl::Mat3 normalMatrix;
  ngl::Mat4 M;
  M = m_mouseGlobalTX * m_transform.getMatrix();
  MV = m_view * M;
  MVP = m_project * MV;
  normalMatrix = MV;
  normalMatrix.inverse().transpose();
  ngl::ShaderLib::setUniform("MV", MV);
  ngl::ShaderLib::setUniform("MVP", MVP);
  ngl::ShaderLib::setUniform("normalMatrix", normalMatrix);
  ngl::ShaderLib::setUniform("M", M);
}

void NGLScene::paintGL()
{
  // clear the screen and depth buffer
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  glViewport(0, 0, m_win.width, m_win.height);
  ngl::ShaderLib::use("Phong");

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
  for (auto &m : m_meshes)
  {
    ngl::Transformation t;
    t.setMatrix(m.tx);
    loadMatricesToShader();
    m.vao->bind();
    m.vao->draw();
    m.vao->unbind();
  }
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
  default:
    break;
  }
  // finally update the GLWindow and re-draw
  // if (isExposed())
  update();
}
