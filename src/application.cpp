#include <iostream>
#include <fstream>
#include <string>
#include <stdio.h>
#include "csapp.h"
#include "application.h"
#include <unistd.h>

#include "dynamic_scene/ambient_light.h"
#include "dynamic_scene/environment_light.h"
#include "dynamic_scene/directional_light.h"
#include "dynamic_scene/area_light.h"
#include "dynamic_scene/point_light.h"
#include "dynamic_scene/spot_light.h"
#include "dynamic_scene/sphere.h"
#include "dynamic_scene/mesh.h"

using Collada::CameraInfo;
using Collada::LightInfo;
using Collada::MaterialInfo;
using Collada::PolymeshInfo;
using Collada::SceneInfo;
using Collada::SphereInfo;

using namespace std;

namespace CMU462 {

// master node code
// uint32_t output[scene_height][scene_width];
std::atomic<int> threadCount;
TaskQueue<Request> workQueue;
sem_t complete_sem;
const int sizeRequest = sizeof(struct Request);
const int sizeResult = sizeof(struct Result);
ImageBuffer *frameBuffer;

// thread function
void *listen_thread(void *vargp);
void *process(void *vargp);
// void generate_work();
// void master_process_request(Request req);


Application::Application(AppConfig config) {
  port = config.port;
  host = config.host;
  pathtracer = new PathTracer (
    config.pathtracer_ns_aa,
    config.pathtracer_max_ray_depth,
    config.pathtracer_ns_area_light,
    config.pathtracer_ns_diff,
    config.pathtracer_ns_glsy,
    config.pathtracer_ns_refr,
    config.pathtracer_num_threads,
    config.pathtracer_envmap
  );
}

Application::~Application() {

  delete cuPathTracer;
  delete pathtracer;

}

void Application::init() {

  if (viewerOn) {
    textManager.init(use_hdpi);
    text_color = Color(1.0, 1.0, 1.0);

    show_coordinates = true;
    show_hud = true;

    // Lighting needs to be explicitly enabled.
    glEnable(GL_LIGHTING);

    // // Enable anti-aliasing and circular points.
    glEnable( GL_LINE_SMOOTH );
    glEnable( GL_POLYGON_SMOOTH );
    glEnable(GL_POINT_SMOOTH);
    glHint( GL_LINE_SMOOTH_HINT, GL_NICEST );
    glHint( GL_POLYGON_SMOOTH_HINT, GL_NICEST );
    glHint(GL_POINT_SMOOTH_HINT,GL_NICEST);

    // Initialize styles (colors, line widths, etc.) that will be used
    // to draw different types of mesh elements in various situations.
    initialize_style();
  }

  // Setup all the basic internal state to default values,
  // as well as some basic OpenGL state (like depth testing
  // and lighting).

  // Set the integer bit vector representing which keys are down.
  leftDown   = false;
  rightDown  = false;
  middleDown = false;



  mode = EDIT_MODE;
  scene = nullptr;

  // Make a dummy camera so resize() doesn't crash before the scene has been
  // loaded.
  // NOTE: there's a chicken-and-egg problem here, because loadScene
  // requires init, and init requires init_camera (which is only called by
  // loadScene).
  screenW = screenH = 600; // Default value
  CameraInfo cameraInfo;
  cameraInfo.hFov = 50;
  cameraInfo.vFov = 35;
  cameraInfo.nClip = 0.01;
  cameraInfo.fClip = 100;
  camera.configure(cameraInfo, screenW, screenH);
}

void Application::initialize_style() {
  // Colors.
  defaultStyle.halfedgeColor = Color( 0.3, 0.3, 0.3, 1.0 );
    hoverStyle.halfedgeColor = Color( 0.6, 0.6, 0.6, 1.0 );
   selectStyle.halfedgeColor = Color( 1.0, 1.0, 1.0, 1.0 );

  defaultStyle.faceColor = Color( 0.3, 0.3, 0.3, 1.0 );
    hoverStyle.faceColor = Color( 0.6, 0.6, 0.6, 1.0 );
   selectStyle.faceColor = Color( 1.0, 1.0, 1.0, 1.0 );

  defaultStyle.edgeColor = Color( 0.3, 0.3, 0.3, 1.0 );
    hoverStyle.edgeColor = Color( 0.6, 0.6, 0.6, 1.0 );
   selectStyle.edgeColor = Color( 1.0, 1.0, 1.0, 1.0 );

  defaultStyle.vertexColor = Color( 0.3, 0.3, 0.3, 1.0 );
    hoverStyle.vertexColor = Color( 0.6, 0.6, 0.6, 1.0 );
   selectStyle.vertexColor = Color( 1.0, 1.0, 1.0, 1.0 );

  // Primitive sizes.
  defaultStyle.strokeWidth = 1.0;
    hoverStyle.strokeWidth = 2.0;
   selectStyle.strokeWidth = 2.0;

  defaultStyle.vertexRadius = 4.0;
    hoverStyle.vertexRadius = 8.0;
   selectStyle.vertexRadius = 8.0;
}

void Application::update_style() {

  float view_distance = (camera.position() - camera.view_point()).norm();
  float scale_factor = canonical_view_distance / view_distance;

    hoverStyle.strokeWidth = 2.0 * scale_factor;
   selectStyle.strokeWidth = 2.0 * scale_factor;

    hoverStyle.vertexRadius = 8.0 * scale_factor;
   selectStyle.vertexRadius = 8.0 * scale_factor;
}

void Application::render() {
  update_gl_camera();
  switch (mode) {
    case EDIT_MODE:
      if (show_coordinates) draw_coordinates();
      scene->render_in_opengl();
      if (show_hud) draw_hud();
      break;
    case VISUALIZE_MODE:
      if (show_coordinates) draw_coordinates();
    case RENDER_MODE:
      pathtracer->update_screen();
      break;
  }
}

void Application::update_gl_camera() {

  // Call resize() every time we draw, since it doesn't seem
  // to get called by the Viewer upon initial window creation
  // (this should probably be fixed!).
  GLint view[4];
  glGetIntegerv(GL_VIEWPORT, view);
  if (view[2] != screenW || view[3] != screenH) {
    resize(view[2], view[3]);
  }

  // Control the camera to look at the mesh.
  glMatrixMode(GL_MODELVIEW);
  glLoadIdentity();

  const Vector3D& c = camera.position();
  const Vector3D& r = camera.view_point();
  const Vector3D& u = camera.up_dir();

  gluLookAt(c.x, c.y, c.z,
            r.x, r.y, r.z,
            u.x, u.y, u.z);
}

void Application::resize(size_t w, size_t h) {
  screenW = w;
  screenH = h;
  camera.set_screen_size(w, h);
  textManager.resize(w, h);
  set_projection_matrix();
  if (mode != EDIT_MODE) {
    pathtracer->set_frame_size(w, h);
  }
}

void Application::set_projection_matrix() {
  glMatrixMode(GL_PROJECTION);
  glLoadIdentity();
  gluPerspective(camera.v_fov(),
                 camera.aspect_ratio(),
                 camera.near_clip(),
                 camera.far_clip());
}

string Application::name() {
  return "PathTracer";
}

string Application::info() {
  switch (mode) {
    case EDIT_MODE:
      return "MeshEdit";
      break;
    case RENDER_MODE:
    case VISUALIZE_MODE:
      return "PathTracer";
      break;
  }
}


    void Application::transferToGPU(){
        cuPathTracer->init();
    }

void Application::load(SceneInfo* sceneInfo) {

  vector<Collada::Node>& nodes = sceneInfo->nodes;
  vector<DynamicScene::SceneLight *> lights;
  vector<DynamicScene::SceneObject *> objects;

  // save camera position to update camera control later
  CameraInfo *c;
  Vector3D c_pos = Vector3D();
  Vector3D c_dir = Vector3D();

  int len = nodes.size();
  for (int i = 0; i < len; i++) {
    Collada::Node& node = nodes[i];
    Collada::Instance *instance = node.instance;
    const Matrix4x4& transform = node.transform;

    switch(instance->type) {
      case Collada::Instance::CAMERA:
        c = static_cast<CameraInfo*>(instance);
        c_pos = (transform * Vector4D(c_pos,1)).to3D();
        c_dir = (transform * Vector4D(c->view_dir,1)).to3D().unit();
        init_camera(*c, transform);
        break;
      case Collada::Instance::LIGHT:
      {
        lights.push_back(
          init_light(static_cast<LightInfo&>(*instance), transform));
        break;
      }
      case Collada::Instance::SPHERE:
        objects.push_back(
          init_sphere(static_cast<SphereInfo&>(*instance), transform));
        break;
      case Collada::Instance::POLYMESH:
        objects.push_back(
          init_polymesh(static_cast<PolymeshInfo&>(*instance), transform));
        break;
      case Collada::Instance::MATERIAL:
        init_material(static_cast<MaterialInfo&>(*instance));
        break;
     }
  }

  scene = new DynamicScene::Scene(objects, lights);

  const BBox& bbox = scene->get_bbox();
  if (!bbox.empty()) {

    Vector3D target = bbox.centroid();
    canonical_view_distance = bbox.extent.norm() / 2 * 1.5;

    double view_distance = canonical_view_distance * 2;
    double min_view_distance = canonical_view_distance / 10.0;
    double max_view_distance = canonical_view_distance * 20.0;

    canonicalCamera.place(target,
                          acos(c_dir.y),
                          atan2(c_dir.x, c_dir.z),
                          view_distance,
                          min_view_distance,
                          max_view_distance);

    camera.place(target,
                acos(c_dir.y),
                atan2(c_dir.x, c_dir.z),
                view_distance,
                min_view_distance,
                max_view_distance);

    set_scroll_rate();
  }

  // set default draw styles for meshEdit -
  scene->set_draw_styles(&defaultStyle, &hoverStyle, &selectStyle);

}

void Application::init_camera(CameraInfo& cameraInfo,
                              const Matrix4x4& transform) {
  camera.configure(cameraInfo, screenW, screenH);
  canonicalCamera.configure(cameraInfo, screenW, screenH);

  if (viewerOn)
    set_projection_matrix();
}

void Application::reset_camera() {
  camera.copy_placement(canonicalCamera);
}

DynamicScene::SceneLight *Application::init_light(LightInfo& light,
                                        const Matrix4x4& transform) {
  switch(light.light_type) {
    case Collada::LightType::NONE:
      break;
    case Collada::LightType::AMBIENT:
      return new DynamicScene::AmbientLight(light);
    case Collada::LightType::DIRECTIONAL:
      return new DynamicScene::DirectionalLight(light, transform);
    case Collada::LightType::AREA:
      return new DynamicScene::AreaLight(light, transform);
    case Collada::LightType::POINT:
      return new DynamicScene::PointLight(light, transform);
    case Collada::LightType::SPOT:
      return new DynamicScene::SpotLight(light, transform);
    default:
      break;
  }
  return nullptr;
}

/**
 * The transform is assumed to be composed of translation, rotation, and
 * scaling, where the scaling is uniform across the three dimensions; these
 * assumptions are necessary to ensure the sphere is still spherical. Rotation
 * is ignored since it's a sphere, translation is determined by transforming the
 * origin, and scaling is determined by transforming an arbitrary unit vector.
 */
DynamicScene::SceneObject *Application::init_sphere(
    SphereInfo& sphere, const Matrix4x4& transform) {
  const Vector3D& position = (transform * Vector4D(0, 0, 0, 1)).projectTo3D();
  double scale = (transform * Vector4D(1, 0, 0, 0)).to3D().norm();
  return new DynamicScene::Sphere(sphere, position, scale);
}

DynamicScene::SceneObject *Application::init_polymesh(
    PolymeshInfo& polymesh, const Matrix4x4& transform) {
  return new DynamicScene::Mesh(polymesh, transform);
}

void Application::set_scroll_rate() {
  scroll_rate = canonical_view_distance / 10;
}

void Application::init_material(MaterialInfo& material) {
  // TODO : Support Materials.
}

void Application::cursor_event(float x, float y) {
  if (leftDown && !middleDown && !rightDown) {
    mouse1_dragged(x, y);
  } else if (!leftDown && !middleDown && rightDown) {
    mouse2_dragged(x, y);
  } else if (!leftDown && !middleDown && !rightDown) {
    mouse_moved(x, y);
  }

  mouseX = x;
  mouseY = y;
}

void Application::scroll_event(float offset_x, float offset_y) {

  update_style();

  switch(mode) {
    case EDIT_MODE:
    case VISUALIZE_MODE:
      camera.move_forward(-offset_y * scroll_rate);
      break;
    case RENDER_MODE:
      break;
  }
}

void Application::mouse_event(int key, int event, unsigned char mods) {
  switch(event) {
    case EVENT_PRESS:
      switch(key) {
        case MOUSE_LEFT:
          mouse_pressed(LEFT);
          break;
        case MOUSE_RIGHT:
          mouse_pressed(RIGHT);
          break;
        case MOUSE_MIDDLE:
          mouse_pressed(MIDDLE);
          break;
      }
      break;
    case EVENT_RELEASE:
      switch(key) {
        case MOUSE_LEFT:
          mouse_released(LEFT);
          break;
        case MOUSE_RIGHT:
          mouse_released(RIGHT);
          break;
        case MOUSE_MIDDLE:
          mouse_released(MIDDLE);
          break;
      }
      break;
  }
}

void Application::keyboard_event(int key, int event, unsigned char mods) {
  switch (mode) {
    case RENDER_MODE:
      if (event == EVENT_PRESS) {
        switch (key) {
        case 'e': case 'E':
            to_edit_mode();
            break;
        case 'v': case 'V':
            pathtracer->stop();
            pathtracer->start_visualizing();
            mode = VISUALIZE_MODE;
            break;
        case 's': case 'S':
            pathtracer->save_image();
            break;
        case '+': case '=':
            pathtracer->stop();
            pathtracer->increase_area_light_sample_count();
            pathtracer->start_raytracing();
            break;
        case '-': case '_':
            pathtracer->stop();
            pathtracer->decrease_area_light_sample_count();
            pathtracer->start_raytracing();
            break;
        case '[': case ']':
            pathtracer->key_press(key);
            break;
        }
      }
      break;
      case VISUALIZE_MODE:
        if (event == EVENT_PRESS) {
        switch(key) {
          case 'e': case 'E':
            to_edit_mode();
            break;
          case 'r': case 'R':
            pathtracer->stop();
            pathtracer->start_raytracing();
            mode = RENDER_MODE;
            break;
          case ' ':
            reset_camera();
            break;
          default:
            pathtracer->key_press(key);
        }
      }
      break;
    case EDIT_MODE:
      if (event == EVENT_PRESS) {
        switch(key) {
          case 'e': case 'E':
            startGPURayTracing();
            //set_up_pathtracer();
            // cuPathTracer = new CUDAPathTracer(pathtracer);
            // transferToGPU();

            // pathtracer->state = PathTracer::RENDERING;
            // pathtracer->continueRaytracing = true;

            // pathtracer->sampleBuffer.clear();
            // pathtracer->frameBuffer.clear();

            // pathtracer->timer.start();
            // cuPathTracer->startRayTracing();
            // pathtracer->timer.stop();
            // fprintf(stdout, "GPU ray tracing done! (%.4f sec)\n", pathtracer->timer.duration());


            // cuPathTracer->updateHostSampleBuffer();
            // delete cuPathTracer;
            // mode = RENDER_MODE;
            break;
          case 'r': case 'R':
            //set_up_pathtracer();
            startCPURayTracing();
            mode = RENDER_MODE;
            break;
          case 'v': case 'V':
            //set_up_pathtracer();
            pathtracer->start_visualizing();
            mode = VISUALIZE_MODE;
            break;
          case ' ':
            reset_camera();
            break;
          case 'h': case 'H':
            show_hud = !show_hud;
            break;
          case 'u': case 'U':
            scene->upsample_selected_mesh();
            break;
          case 'd': case 'D':
            scene->downsample_selected_mesh();
            break;
          case 'i': case 'I':
            // i for isotropic.
            scene->resample_selected_mesh();
            break;
          case 'f': case 'F':
            scene->flip_selected_edge();
            break;
          case 's': case 'S':
            scene->split_selected_edge();
            break;
          case 'c': case 'C':
            scene->collapse_selected_edge();
            break;
          case 'm': case 'M':
            saveCamera();
            break;
          default:
            break;
        }
      }
      break;
  }
}

void Application::mouse_pressed(e_mouse_button b) {
  switch (b) {
    case LEFT:
      if (mode == EDIT_MODE) {
        if (scene->has_hover()) {
          scene->confirm_selection();
        } else {
          scene->invalidate_selection();
        }
      }
      leftDown = true;
      break;
    case RIGHT:
      rightDown = true;
      break;
    case MIDDLE:
      middleDown = true;
      break;
  }
}

void Application::mouse_released(e_mouse_button b) {
  switch (b) {
    case LEFT:
      leftDown = false;
      break;
    case RIGHT:
      rightDown = false;
      break;
    case MIDDLE:
      middleDown = false;
      break;
  }
}

/*
  When in edit mode and there is a selection, move the selection.
  When in visualization mode, rotate.
*/
void Application::mouse1_dragged(float x, float y) {
  if (mode == RENDER_MODE) {
    return;
  }
  float dx = (x - mouseX);
  float dy = (y - mouseY);

  if (mode == EDIT_MODE && scene->has_selection()) {
    scene->drag_selection(2 * dx / screenW, 2 * -dy / screenH,
                          get_world_to_3DH());
  } else {
    camera.rotate_by(dy * (PI / screenH), dx * (PI / screenW));
  }
}

/*
  When the mouse is dragged with the right button held down, translate.
*/
void Application::mouse2_dragged(float x, float y) {
  if (mode == RENDER_MODE) return;
  float dx = (x - mouseX);
  float dy = (y - mouseY);

  // don't negate y because up is down.
  camera.move_by(-dx, dy, canonical_view_distance);
}

void Application::mouse_moved(float x, float y) {
  if (mode != EDIT_MODE) return;
  y = screenH - y; // Because up is down.
  // Converts x from [0, w] to [-1, 1], and similarly for y.
  Vector2D p(x * 2 / screenW - 1, y * 2 / screenH - 1);
  scene->update_selection(p, get_world_to_3DH());
}

void Application::to_edit_mode() {
  if (mode == EDIT_MODE) return;
  pathtracer->stop();
  pathtracer->clear();
  mode = EDIT_MODE;
  mouse_moved(mouseX, mouseY);
}

void Application::set_up_pathtracer() {
  if (mode != EDIT_MODE) return;
  pathtracer->set_camera(&camera);
  pathtracer->set_scene(scene->get_static_scene());
  pathtracer->set_frame_size(screenW, screenH);

  // Ray ray = pathtracer->camera->generate_ray(500.0 / screenW, 300.0 / screenH);
  // printf("T: %f %f %f\n", ray.o[0], ray.o[1], ray.o[2]);
  // printf("T: %f %f %f\n", ray.d[0], ray.d[1], ray.d[2]);
}

Matrix4x4 Application::get_world_to_3DH() {
  Matrix4x4 P, M;
  glGetDoublev(GL_PROJECTION_MATRIX, &P(0, 0));
  glGetDoublev(GL_MODELVIEW_MATRIX, &M(0, 0));
  return P * M;
}

inline void Application::draw_string(float x, float y,
  string str, size_t size, const Color& c) {
  int line_index = textManager.add_line(( x * 2 / screenW) - 1.0,
                                        (-y * 2 / screenH) + 1.0,
                                        str, size, c);
  messages.push_back(line_index);
}

void Application::draw_coordinates() {

  glDisable(GL_DEPTH_TEST);
  glDisable(GL_LIGHTING);

  glBegin(GL_LINES);
  glColor4f(1.0f, 0.0f, 0.0f, 0.5f);
  glVertex3i(0,0,0);
  glVertex3i(1,0,0);

  glColor4f(0.0f, 1.0f, 0.0f, 0.5f);
  glVertex3i(0,0,0);
  glVertex3i(0,1,0);

  glColor4f(0.0f, 0.0f, 1.0f, 0.5f);
  glVertex3i(0,0,0);
  glVertex3i(0,0,1);

  glColor4f(0.5f, 0.5f, 0.5f, 0.5f);
  for (int x = 0; x <= 8; ++x) {
    glVertex3i(x - 4, 0, -4);
    glVertex3i(x - 4, 0,  4);
  }
  for (int z = 0; z <= 8; ++z) {
    glVertex3i(-4, 0, z - 4);
    glVertex3i( 4, 0, z - 4);
  }
  glEnd();

  glEnable(GL_LIGHTING);
  glEnable(GL_DEPTH_TEST);

}

void Application::draw_hud() {
  textManager.clear();
  messages.clear();

  const size_t size = 16;
  const float x0 = use_hdpi ? screenW - 300 * 2 : screenW - 300;
  const float y0 = use_hdpi ? 128 : 64;
  const int inc  = use_hdpi ? 48  : 24;
  float y = y0 + inc - size;

  // No selection --> no messages.
  if (!scene->has_selection()) {
    draw_string(x0, y, "No mesh feature is selected", size, text_color);
    y += inc;
  } else {
    DynamicScene::SelectionInfo *selectionInfo = scene->get_selection_info();
    for (const string& s : selectionInfo->info) {
      size_t split = s.find_first_of(":");
      if (split != string::npos) {
        split++;
        string s1 = s.substr(0,split);
        string s2 = s.substr(split);
        draw_string(x0, y, s1, size, text_color);
        draw_string(x0 + (use_hdpi ? 150 : 75 ), y, s2, size, text_color);
      } else {
        draw_string(x0, y, s, size, text_color);
      }
      y += inc;
    }
  }

  // -- First draw a lovely black rectangle.

  glPushAttrib(GL_VIEWPORT_BIT);
  glViewport(0, 0, screenW, screenH);

  glMatrixMode(GL_PROJECTION);
  glPushMatrix();
  glLoadIdentity();
  glOrtho(0, screenW, screenH, 0, 0, 1);

  glMatrixMode(GL_MODELVIEW);
  glPushMatrix();
  glLoadIdentity();
  glTranslatef(0, 0, -1);

  // -- Black with opacity .8;

  glColor4f(0.0, 0.0, 0.0, 0.8);

  float min_x = x0 - 32;
  float min_y = y0 - 32;
  float max_x = screenW;
  float max_y = y;

  float z = 0.0;

  glDisable(GL_DEPTH_TEST);
  glDisable(GL_LIGHTING);

  glBegin(GL_QUADS);

  glVertex3f(min_x, min_y, z);
  glVertex3f(min_x, max_y, z);
  glVertex3f(max_x, max_y, z);
  glVertex3f(max_x, min_y, z);
  glEnd();

  glMatrixMode(GL_PROJECTION);
  glPopMatrix();

  glMatrixMode(GL_MODELVIEW);
  glPopMatrix();

  glPopAttrib();

  glEnable(GL_LIGHTING);
  glEnable(GL_DEPTH_TEST);

  textManager.render();
}

void Application::startGPURayTracing() {
  // original initialization
  frameBuffer = &pathtracer->frameBuffer;
  cuPathTracer = new CUDAPathTracer(pathtracer);
  transferToGPU();

  pathtracer->state = PathTracer::RENDERING;
  pathtracer->continueRaytracing = true;

  pathtracer->sampleBuffer.clear();
  pathtracer->frameBuffer.clear();

  int clientfd;
  rio_t rio;
  char *Host, *Port;
  Host = new char[host.length() + 1];
  Port = new char[port.length() + 1];
  strcpy(Host, host.c_str());
  strcpy(Port, port.c_str());

  printf("Prepare to connect to %s:%s\n", Host, Port);
  clientfd = open_clientfd(Host, Port);
  while (clientfd < 0) {
    usleep(10000);
    clientfd = open_clientfd(Host, Port);
  }
  printf("Connected to master\n");
  Rio_readinitb(&rio, clientfd);

  // worker main
  // pathtracer->timer.start();
  char requestBuf[sizeRequest];
  char resultBuf[sizeResult];

  Request req;
  Result result;
  int w = frameBuffer->w;

  while(rio_readnb(&rio, requestBuf, sizeRequest) > 0) {
    memcpy(&req, requestBuf, sizeRequest);
    int dataSize = req.xRange * req.yRange;
    int k = 0;
    printf("worker process START [x: %d, y: %d, xRange: %d, yRange: %d]\n", req.x, req.y, req.xRange, req.yRange);
    // process request
    master_process_request(req);
    cuPathTracer->updateHostSampleBuffer(req);

    for (int y = req.y; y < req.y + req.yRange; y++) {
      for (int x = req.x; x < req.x + req.xRange; x++) {
        result.data[k % DATA_SIZE] = frameBuffer->data[y * w + x];
        k++;
        if (k % DATA_SIZE == 0 || k == dataSize) {
          memcpy(resultBuf, &result, sizeResult);
          rio_writen(clientfd, resultBuf, sizeResult);
        }
      }
    }
    printf("worker process  DONE [x: %d, y: %d, xRange: %d, yRange: %d]\n", req.x, req.y, req.xRange, req.yRange);
  }
  close(clientfd);

  // cuPathTracer->startRayTracingPT();
  // pathtracer->timer.stop();



  // fprintf(stdout, "GPU ray tracing done! (%.4f sec)\n", pathtracer->timer.duration());
  printf("Work done!\n");

  // cuPathTracer->updateHostSampleBuffer();
  delete []Host;
  delete []Port;

  delete cuPathTracer;
  mode = RENDER_MODE;
}

void Application::startCPURayTracing() {

    pathtracer->start_raytracing();
}
// void Camera::copy_placement(const Camera& other) {
//   pos = other.pos;
//   targetPos = other.targetPos;
//   phi = other.phi;
//   theta = other.theta;
//   minR = other.minR;
//   maxR = other.maxR;
//   c2w = other.c2w;
// }
void Application::saveCamera() {
  time_t rawtime;
  time (&rawtime);

  string filename = "camera_";
  filename += string(ctime(&rawtime));
  filename.erase(filename.end() - 1);
  filename += string(".info");

  ofstream cameraFile;
  cameraFile.open(filename);
  cameraFile << pathtracer->camera->pos << endl;
  cameraFile << pathtracer->camera->targetPos << endl;
  cameraFile << pathtracer->camera->phi << endl;
  cameraFile << pathtracer->camera->theta << endl;
  cameraFile << pathtracer->camera->minR << endl;
  cameraFile << pathtracer->camera->maxR << endl;
  cameraFile << pathtracer->camera->c2w << endl;
  cameraFile.close();
  fprintf(stderr, "[Camera Info] Saving to file: %s... ", filename.c_str());
}

void Application::loadCamera(string filename) {
  ifstream fin;
  char c;
  Camera& cam = *(pathtracer->camera);

  FILE * pFile;
  pFile = fopen (filename.c_str(),"r");
  if (pFile==NULL)
  {
      cout << "fail!" << endl;
  }

  fscanf(pFile, "%lf %lf %lf", &cam.pos[0], &cam.pos[1], &cam.pos[2]);
  fscanf(pFile, "%lf %lf %lf", &cam.targetPos[0], &cam.targetPos[1], &cam.targetPos[2]);
  fscanf(pFile, "%lf", &cam.phi);
  fscanf(pFile, "%lf", &cam.theta);
  fscanf(pFile, "%lf", &cam.minR);
  fscanf(pFile, "%lf", &cam.maxR);
  fscanf(pFile, "%lf %lf %lf %lf %lf %lf %lf %lf %lf", &cam.c2w(0, 0), &cam.c2w(0, 1), &cam.c2w(0, 2),
            &cam.c2w(1, 0), &cam.c2w(1, 1), &cam.c2w(1, 2), &cam.c2w(2, 0), &cam.c2w(2, 1), &cam.c2w(2, 2));

  // cout << cam.pos << endl;
  // cout << cam.targetPos << endl;
  // cout << cam.phi << endl;
  // cout << cam.theta << endl;
  // cout << cam.minR << endl;
  // cout << cam.maxR << endl;
  // cout << cam.c2w << endl;

  fclose(pFile);
}

void Application::generate_work() {
  int w = pathtracer->frameBuffer.w;
  int h = pathtracer->frameBuffer.h;
  int rowNum = (h + tileSize - 1) / tileSize;
  int colNum = (w + tileSize - 1) / tileSize;

  for (int r = 0; r < rowNum; r++) {
    for (int c = 0; c < colNum; c++) {
      Request req;
      req.x = c * tileSize;
      req.y = r * tileSize;
      req.xRange = std::min(tileSize, w - req.x);
      req.yRange = std::min(tileSize, h - req.y);
      workQueue.put_work(req);
    }
  }
}

void Application::master_process_request(Request req) {
  cuPathTracer->processRequest(req);
  // update framebuffer
}

void *listen_thread(void *vargp) {
  printf("Enter listen thread\n");
  int listenfd, *connfdp;
  socklen_t clientlen;
  struct sockaddr_storage clientaddr;
  pthread_t tid;

  listenfd = open_listenfd((char*)vargp);
  
  printf("Master node listening on port: %s\n", (char*)vargp);

  while (true) {
    clientlen = sizeof(struct sockaddr_storage);
    connfdp = (int*)malloc(sizeof(int));
    *connfdp = accept(listenfd, (SA*)&clientaddr, &clientlen);
    pthread_create(&tid, NULL, process, connfdp);
  }

  return NULL;
}

void *process(void *vargp) {
  threadCount++;

  int connfd = *((int *)vargp);
  pthread_detach(pthread_self());
  free(vargp);

  char requestBuf[sizeRequest];
  char resultBuf[sizeResult];
  Result result;

  rio_t rio;
  rio_readinitb(&rio, connfd);

  while(1) {
    bool rt;
    Request req = workQueue.get_work(rt);
    if (!rt) { // work queue is empty
      break;
    }
    printf("thread process START [x: %d, y: %d, xRange: %d, yRange: %d]\n", req.x, req.y, req.xRange, req.yRange);
    memcpy(requestBuf, &req, sizeRequest);
    rio_writen(connfd, requestBuf, sizeRequest);

    // worker node process request and return result

    // compute how much result to receive
    int dataSize = req.xRange * req.yRange;
    int k = 0;
    int w = frameBuffer->w;
    // receive result from worker
    for (int y = req.y; y < req.y + req.yRange; y++) {
      for (int x = req.x; x < req.x + req.xRange; x++) {
        if (k % DATA_SIZE == 0) {
          rio_readnb(&rio, resultBuf, sizeResult);
          memcpy(&result, resultBuf, sizeResult);
        }
        frameBuffer->data[y * w + x] = result.data[k % DATA_SIZE];
        k++;
      }
    }

    printf("thread process  DONE [x: %d, y: %d, xRange: %d, yRange: %d]\n", req.x, req.y, req.xRange, req.yRange);
  }
  close(connfd);

  threadCount--;
  if (threadCount == 0) {
    sem_post(&complete_sem);
  }
  return NULL;
}
} // namespace CMU462
