#include "strata_core/grid2d_backend.hpp"
#include <algorithm>
#include <cstdlib>
namespace strata_core {
Grid2DBackend::Grid2DBackend(const GridMeta& meta, LayeredMapParams params)
  : meta_(meta), layered_(params),
    flags_(static_cast<std::size_t>(std::max(0, meta.width)) * std::max(0, meta.height), 0) {}
void Grid2DBackend::raycastClear(int gx0,int gy0,int gx1,int gy1){
  int dx=std::abs(gx1-gx0), dy=-std::abs(gy1-gy0);
  int sx=gx0<gx1?1:-1, sy=gy0<gy1?1:-1, err=dx+dy, x=gx0, y=gy0;
  while(true){
    if(x==gx1&&y==gy1) break;
    if(x>=0&&y>=0&&x<meta_.width&&y<meta_.height){
      const CellId id=gridCellId(meta_,x,y); layered_.observeMiss(id);
      if(!(flags_[id]&(kHitOpen|kMissOpen))) touched_open_.push_back(id);
      flags_[id]|=kFree|kMissOpen; }
    int e2=2*err; if(e2>=dy){err+=dy;x+=sx;} if(e2<=dx){err+=dx;y+=sy;}
  }
}
void Grid2DBackend::integrate(const Observation& obs, const Eigen::Vector3d& sensor_origin_map){
  int sx,sy; const bool ok=worldToGrid(meta_, sensor_origin_map.x(), sensor_origin_map.y(), sx, sy);
  for(const auto& h: obs.hits){ int gx,gy; if(!worldToGrid(meta_,h.x(),h.y(),gx,gy)) continue;
    if(ok) raycastClear(sx,sy,gx,gy);
    const CellId id=gridCellId(meta_,gx,gy); layered_.observeHit(id);
    if(!(flags_[id]&(kHitOpen|kMissOpen))) touched_open_.push_back(id);
    flags_[id]|=kHitOpen; }
}
bool Grid2DBackend::tick(){
  if(!layered_.tick()) return false;
  onWindowClosed();
  return true;
}
void Grid2DBackend::closeWindows(int k){
  if(k<=0) return;
  layered_.closeWindows(k);
  onWindowClosed();   // the k - 1 empty windows observed no cell, so they change no flag
}
void Grid2DBackend::onWindowClosed(){
  // Window closed: each cell it observed records whether that observation was a hit
  // (a hit wins over a clear, as in LayeredMap). Unobserved cells keep their record.
  for(CellId id: touched_open_){
    std::uint8_t f=flags_[id];
    f=(f&kHitOpen) ? static_cast<std::uint8_t>(f|kHitLast) : static_cast<std::uint8_t>(f&~kHitLast);
    flags_[id]=static_cast<std::uint8_t>(f&~(kHitOpen|kMissOpen));
  }
  touched_open_.clear();
}
bool Grid2DBackend::observedFree(int gx, int gy) const {
  if(gx<0||gy<0||gx>=meta_.width||gy>=meta_.height) return false;
  return (flags_[gridCellId(meta_,gx,gy)]&kFree)!=0;
}
GridMap Grid2DBackend::toOccupancyGrid() const {
  GridMap g; g.meta=meta_; g.data.assign((std::size_t)meta_.width*meta_.height, -1);
  auto put=[&](CellId id, std::int8_t v){ if(id>=0 && id<(CellId)g.data.size()) g.data[id]=v; };
  // Ascending priority, so a later class overwrites: free < transient < periodic < static.
  // A cell hit in the open window or last observed as a hit is an obstacle, whatever
  // its accumulated evidence (a new object on long-cleared ground has negative
  // log-odds); a cell a ray has cleared and last observed free is free.
  for(std::size_t id=0; id<flags_.size(); ++id){
    const std::uint8_t f=flags_[id];
    if(f&(kHitOpen|kHitLast)) g.data[id]=50;
    else if(f&kFree) g.data[id]=0;
  }
  // Otherwise a Transient cell is drawn as an obstacle only while its evidence leans
  // occupied; one that leans free (or is undecided at p = 0.5) keeps 0 or -1.
  for(CellId id: layered_.transientCells()) if(layered_.occupancyProb(id)>0.5) put(id,50);
  for(CellId id: layered_.periodicCells())  put(id,75);
  for(CellId id: layered_.staticCells())    put(id,100);
  return g;
}
}  // namespace strata_core
