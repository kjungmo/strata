#include "prism_map_core/grid2d_backend.hpp"
#include <cstdlib>
namespace prism_map_core {
Grid2DBackend::Grid2DBackend(const GridMeta& meta, LayeredMapParams params)
  : meta_(meta), layered_(params) {}
void Grid2DBackend::raycastClear(int gx0,int gy0,int gx1,int gy1){
  int dx=std::abs(gx1-gx0), dy=-std::abs(gy1-gy0);
  int sx=gx0<gx1?1:-1, sy=gy0<gy1?1:-1, err=dx+dy, x=gx0, y=gy0;
  while(true){
    if(x==gx1&&y==gy1) break;
    if(x>=0&&y>=0&&x<meta_.width&&y<meta_.height) layered_.observeMiss(gridCellId(meta_,x,y));
    int e2=2*err; if(e2>=dy){err+=dy;x+=sx;} if(e2<=dx){err+=dx;y+=sy;}
  }
}
void Grid2DBackend::integrate(const Observation& obs, const Eigen::Vector3d& sensor_origin_map){
  int sx,sy; const bool ok=worldToGrid(meta_, sensor_origin_map.x(), sensor_origin_map.y(), sx, sy);
  for(const auto& h: obs.hits){ int gx,gy; if(!worldToGrid(meta_,h.x(),h.y(),gx,gy)) continue;
    if(ok) raycastClear(sx,sy,gx,gy); layered_.observeHit(gridCellId(meta_,gx,gy)); }
}
bool Grid2DBackend::tick(){ return layered_.tick(); }
GridMap Grid2DBackend::toOccupancyGrid() const {
  GridMap g; g.meta=meta_; g.data.assign((std::size_t)meta_.width*meta_.height, -1);
  auto put=[&](CellId id, std::int8_t v){ if(id>=0 && id<(CellId)g.data.size()) g.data[id]=v; };
  for(CellId id: layered_.transientCells()) put(id,50);
  for(CellId id: layered_.periodicCells())  put(id,75);
  for(CellId id: layered_.staticCells())    put(id,100);
  return g;
}
}  // namespace prism_map_core
