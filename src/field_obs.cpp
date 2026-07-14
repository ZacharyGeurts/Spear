// SPDX-License-Identifier: MIT
// field-obs — Field capture (OBS rewrite, C++ only, tiny)
// Cams · snap · rec · screen (optional X11) · no Electron · GitHub-cap friendly
#include "spear_common.hpp"

#include <dirent.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <linux/videodev2.h>

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using namespace spear;

static void usage() {
  std::puts(
      "field-obs — Field capture (OBS rewrite · C++ · tiny)\n"
      "  field-obs cams                 list /dev/video*\n"
      "  field-obs snap [dev] [out.ppm] one frame → PPM\n"
      "  field-obs rec  [dev] [n] [out] n frames raw YUYV\n"
      "  field-obs status\n"
      "Doctrine: Field secure · no cloud · operator host only · God Bless");
}

static std::vector<std::string> list_cams() {
  std::vector<std::string> out;
  DIR* d = ::opendir("/dev");
  if (!d) return out;
  while (dirent* e = ::readdir(d)) {
    if (std::strncmp(e->d_name, "video", 5) != 0) continue;
    if (!is_digits(e->d_name + 5) && e->d_name[5] != '\0') {
      // video0, video1 ok; skip video-something weird
      bool ok = true;
      for (const char* p = e->d_name + 5; *p; ++p)
        if (*p < '0' || *p > '9') {
          ok = false;
          break;
        }
      if (!ok) continue;
    }
    out.push_back(std::string("/dev/") + e->d_name);
  }
  ::closedir(d);
  std::sort(out.begin(), out.end());
  return out;
}

static int cmd_cams() {
  auto cams = list_cams();
  std::printf("field-obs cams · %zu device(s)\n", cams.size());
  for (const auto& c : cams) {
    int fd = ::open(c.c_str(), O_RDWR | O_NONBLOCK);
    std::string name = "?";
    if (fd >= 0) {
      v4l2_capability cap{};
      if (ioctl(fd, VIDIOC_QUERYCAP, &cap) == 0) name = reinterpret_cast<char*>(cap.card);
      ::close(fd);
    }
    std::printf("  %s  %s\n", c.c_str(), name.c_str());
  }
  if (cams.empty()) std::puts("  (none — plug a camera or load a v4l2 loopback)");
  return 0;
}

static bool yuyv_to_ppm(const uint8_t* yuyv, int w, int h, const char* path) {
  FILE* f = std::fopen(path, "wb");
  if (!f) return false;
  std::fprintf(f, "P6\n%d %d\n255\n", w, h);
  for (int i = 0; i < w * h; i += 2) {
    int y0 = yuyv[i * 2 + 0];
    int u = yuyv[i * 2 + 1] - 128;
    int y1 = yuyv[i * 2 + 2];
    int v = yuyv[i * 2 + 3] - 128;
    auto put = [&](int y) {
      int r = y + (int)(1.402 * v);
      int g = y - (int)(0.344 * u + 0.714 * v);
      int b = y + (int)(1.772 * u);
      auto cl = [](int x) { return x < 0 ? 0 : x > 255 ? 255 : x; };
      std::fputc(cl(r), f);
      std::fputc(cl(g), f);
      std::fputc(cl(b), f);
    };
    put(y0);
    put(y1);
  }
  std::fclose(f);
  return true;
}

static int open_cam(const char* dev, int& w, int& h, uint32_t& fmt) {
  int fd = ::open(dev, O_RDWR);
  if (fd < 0) {
    std::perror(dev);
    return -1;
  }
  v4l2_format f{};
  f.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  if (ioctl(fd, VIDIOC_G_FMT, &f) != 0) {
    // try set
    f.fmt.pix.width = 640;
    f.fmt.pix.height = 480;
    f.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;
    f.fmt.pix.field = V4L2_FIELD_NONE;
    if (ioctl(fd, VIDIOC_S_FMT, &f) != 0) {
      std::perror("VIDIOC_S_FMT");
      ::close(fd);
      return -1;
    }
  }
  w = f.fmt.pix.width;
  h = f.fmt.pix.height;
  fmt = f.fmt.pix.pixelformat;
  return fd;
}

static int cmd_snap(int argc, char** argv) {
  auto cams = list_cams();
  std::string dev = cams.empty() ? "/dev/video0" : cams[0];
  std::string out = "field-snap.ppm";
  if (argc >= 3 && argv[2][0] == '/') dev = argv[2];
  else if (argc >= 3) dev = argv[2];
  if (argc >= 4) out = argv[3];
  int w = 0, h = 0;
  uint32_t fmt = 0;
  int fd = open_cam(dev.c_str(), w, h, fmt);
  if (fd < 0) return 1;
  size_t need = (size_t)w * h * 2;
  std::vector<uint8_t> buf(need);
  // request mmap buffer
  v4l2_requestbuffers req{};
  req.count = 1;
  req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  req.memory = V4L2_MEMORY_MMAP;
  bool use_mmap = ioctl(fd, VIDIOC_REQBUFS, &req) == 0 && req.count >= 1;
  if (use_mmap) {
    v4l2_buffer b{};
    b.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    b.memory = V4L2_MEMORY_MMAP;
    b.index = 0;
    if (ioctl(fd, VIDIOC_QUERYBUF, &b) != 0) use_mmap = false;
    void* start = nullptr;
    if (use_mmap) {
      start = ::mmap(nullptr, b.length, PROT_READ | PROT_WRITE, MAP_SHARED, fd, b.m.offset);
      if (start == MAP_FAILED) use_mmap = false;
    }
    if (use_mmap) {
      ioctl(fd, VIDIOC_QBUF, &b);
      int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
      ioctl(fd, VIDIOC_STREAMON, &type);
      if (ioctl(fd, VIDIOC_DQBUF, &b) == 0) {
        size_t n = b.bytesused < need ? b.bytesused : need;
        std::memcpy(buf.data(), start, n);
      }
      ioctl(fd, VIDIOC_STREAMOFF, &type);
      ::munmap(start, b.length);
    }
  }
  if (!use_mmap) {
    // read() path
    ssize_t n = ::read(fd, buf.data(), buf.size());
    if (n <= 0) {
      std::perror("read");
      ::close(fd);
      return 1;
    }
  }
  ::close(fd);
  if (fmt != V4L2_PIX_FMT_YUYV && fmt != 0) {
    // still try yuyv convert
  }
  if (!yuyv_to_ppm(buf.data(), w, h, out.c_str())) {
    std::perror(out.c_str());
    return 1;
  }
  std::printf("field-obs snap OK %s %dx%d → %s\n", dev.c_str(), w, h, out.c_str());
  return 0;
}

static int cmd_rec(int argc, char** argv) {
  auto cams = list_cams();
  std::string dev = cams.empty() ? "/dev/video0" : cams[0];
  int nframes = 30;
  std::string out = "field-rec.yuyv";
  if (argc >= 3) dev = argv[2];
  if (argc >= 4) nframes = std::atoi(argv[3]);
  if (argc >= 5) out = argv[4];
  if (nframes < 1) nframes = 1;
  if (nframes > 600) nframes = 600;
  int w = 0, h = 0;
  uint32_t fmt = 0;
  int fd = open_cam(dev.c_str(), w, h, fmt);
  if (fd < 0) return 1;
  size_t frame = (size_t)w * h * 2;
  FILE* f = std::fopen(out.c_str(), "wb");
  if (!f) {
    std::perror(out.c_str());
    ::close(fd);
    return 1;
  }
  std::fprintf(f, "FIELDRAW1 %d %d YUYV\n", w, h);
  std::vector<uint8_t> buf(frame);
  int got = 0;
  for (int i = 0; i < nframes; ++i) {
    ssize_t n = ::read(fd, buf.data(), buf.size());
    if (n <= 0) break;
    std::fwrite(buf.data(), 1, (size_t)n, f);
    ++got;
  }
  std::fclose(f);
  ::close(fd);
  std::printf("field-obs rec OK frames=%d %dx%d → %s\n", got, w, h, out.c_str());
  return 0;
}

static int cmd_status() {
  auto cams = list_cams();
  std::printf("field-obs status\n");
  std::printf("  stack: C++ · OBS rewrite · no Electron\n");
  std::printf("  cams: %zu\n", cams.size());
  std::printf("  secure: local device only · no cloud ingest\n");
  std::printf("  sister: field-gimp · field-nvtop · fieldbox\n");
  std::printf("  github_cap: tiny ELF · fits Field product ISO\n");
  return 0;
}

int main(int argc, char** argv) {
  if (argc < 2) {
    usage();
    return 0;
  }
  std::string cmd = argv[1];
  if (cmd == "cams" || cmd == "list") return cmd_cams();
  if (cmd == "snap" || cmd == "capture") return cmd_snap(argc, argv);
  if (cmd == "rec" || cmd == "record") return cmd_rec(argc, argv);
  if (cmd == "status") return cmd_status();
  if (cmd == "-h" || cmd == "--help" || cmd == "help") {
    usage();
    return 0;
  }
  usage();
  return 1;
}
