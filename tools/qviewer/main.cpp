#include "viewer.h"
#include <args/args.hxx>
#include <iostream>

void banner() {
  std::cout << R"(
   ██████╗ ██╗   ██╗██╗███████╗██╗    ██╗███████╗██████╗
  ██╔═══██╗██║   ██║██║██╔════╝██║    ██║██╔════╝██╔══██╗
  ██║   ██║██║   ██║██║█████╗  ██║ █╗ ██║█████╗  ██████╔╝
  ██║▄▄ ██║╚██╗ ██╔╝██║██╔══╝  ██║███╗██║██╔══╝  ██╔══██╗
  ╚██████╔╝ ╚████╔╝ ██║███████╗╚███╔███╔╝███████╗██║  ██║
   ╚══▀▀═╝   ╚═══╝  ╚═╝╚══════╝ ╚══╝╚══╝ ╚══════╝╚═╝  ╚═╝
                Quake 1 Map and BSP Viewer
  -------------------------------------------------------
)";
}

int main(int argc, char *argv[]) {
  banner();

  args::ArgumentParser parser("qviewer - view quake 1 map and bsp files");
  args::HelpFlag help(parser, "help", "show this help", {'h', "help"});
  args::ValueFlag<std::string> map(parser, "file", "map or bsp file", {'m'});
  args::ValueFlagList<std::string> wads(
      parser, "path",
      "wad folder or .wad file, can be repeated. the wads named by the map are "
      "used first, all others as a pool",
      {'w'});
  args::Flag noBake(parser, "no-bake", "do not bake the lights of .map files", {"no-bake"});
  args::Flag fullbright(parser, "fullbright", "start without lightmap", {"fullbright"});
  args::Flag wireframe(parser, "wireframe", "start with the line overlay", {"wireframe"});
  args::Flag fog(parser, "fog", "fog even if the map has no fog key", {"fog"});
  args::Flag noBloom(parser, "no-bloom", "start without bloom", {"no-bloom"});
  args::Flag lightsOff(parser, "lights-off", "start with switchable lights off", {"lights-off"});
  args::ValueFlag<std::string> screenshot(parser, "file", "save a screenshot and quit", {"screenshot"});
  args::ValueFlag<int> frames(parser, "count", "frames before the screenshot (default 30)", {"frames"});
  args::ValueFlag<double> styleTime(parser, "seconds", "freeze the light styles at this time",
                                    {"style-time"});

  try {
    parser.ParseCLI(argc, argv);
  } catch (const args::Help &) {
    std::cout << parser;
    return 0;
  } catch (const args::Error &e) {
    std::cerr << e.what() << std::endl << parser;
    return 1;
  }
  if (!map) {
    std::cout << parser;
    return 0;
  }

  LoadOptions load;
  load.wadPaths = args::get(wads);
  load.bake = !noBake;

  ViewSettings view;
  view.fullbright = fullbright;
  view.wireframe = wireframe;
  view.fog.enabled = fog;
  view.bloom = !noBloom;
  view.switchableLights = !lightsOff;

  CaptureOptions capture;
  capture.file = screenshot ? args::get(screenshot) : "";
  capture.frames = frames ? args::get(frames) : capture.frames;
  capture.lightStyleTime = styleTime ? args::get(styleTime) : -1;

  Viewer viewer;
  if (!viewer.Load(args::get(map), load))
    return 1;
  viewer.Run(view, capture);
  return 0;
}
