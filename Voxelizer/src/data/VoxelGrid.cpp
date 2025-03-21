#include <data/VoxelGrid.h>

#include <iostream>

void VoxelGrid::initialize(float resolution, const Eigen::Vector4f& min, const Eigen::Vector4f& max) {
  clear();

  resolution_ = resolution;
  sizex_ = std::ceil((max.x() - min.x()) / resolution_);
  sizey_ = std::ceil((max.y() - min.y()) / resolution_);
  sizez_ = std::ceil((max.z() - min.z()) / resolution_);

  voxels_.resize(sizex_ * sizey_ * sizez_);
  // ensure that min, max are always inside the voxel grid.
  float ox = min.x() - 0.5 * (sizex_ * resolution - (max.x() - min.x()));
  float oy = min.y() - 0.5 * (sizey_ * resolution - (max.y() - min.y()));
  float oz = min.z() - 0.5 * (sizez_ * resolution - (max.z() - min.z()));
  offset_ = Eigen::Vector4f(ox, oy, oz, 1);

  //    center_.head(3) = 0.5 * (max - min) + min;
  //    center_[3] = 0;

  occlusions_.resize(sizex_ * sizey_ * sizez_);
  occludedBy_.resize(sizex_ * sizey_ * sizez_);

  std::cout << "[Voxelgrid::initialize] " << resolution_ << "; num. voxels = [" << sizex_ << ", " << sizey_ << ", "
            << sizez_ << "], maxExtent = " << max.transpose() << ", minExtent" << min.transpose() << std::endl;
}

void VoxelGrid::clear() {
  for (auto idx : occupied_) {
    voxels_[idx].count = 0;
    voxels_[idx].labels.clear();
  }

  //  for (auto idx : occluded_) {
  //    voxels_[idx].count = 0;
  //    voxels_[idx].labels.clear();
  //  }

  occupied_.clear();
  //  occluded_.clear();
}

void VoxelGrid::insert(const Eigen::Vector4f& p, uint32_t label) {
  Eigen::Vector4f tp = p - offset_;
  int32_t i = std::floor(tp.x() / resolution_);
  int32_t j = std::floor(tp.y() / resolution_);
  int32_t k = std::floor(tp.z() / resolution_);

  if ((i >= int32_t(sizex_)) || (j >= int32_t(sizey_)) || (k >= int32_t(sizez_))) return;
  if ((i < 0) || (j < 0) || (k < 0)) return;

  int32_t gidx = index(i, j, k);
  if (gidx < 0 || gidx >= int32_t(voxels_.size())) return;

  if (voxels_[gidx].count == 0) occupied_.push_back(gidx);

  //    float n = voxels_[gidx].count;
  voxels_[gidx].labels[label] += 1;  //(1. / (n + 1)) * (n * voxels_[gidx].point + p);
  voxels_[gidx].count += 1;
  occlusionsValid_ = false;
}

bool VoxelGrid::isOccluded(int32_t i, int32_t j, int32_t k) const { return occlusions_[index(i, j, k)] > -1; }

bool VoxelGrid::isFree(int32_t i, int32_t j, int32_t k) const { return occlusions_[index(i, j, k)] == -1; }

bool VoxelGrid::isInvalid(int32_t i, int32_t j, int32_t k) const {
  if (int32_t(invalid_.size()) <= index(i, j, k)) return true;

  return (invalid_[index(i, j, k)] > -1) && (invalid_[index(i, j, k)] != index(i, j, k));
}

void VoxelGrid::insertOcclusionLabels() {
  if (!occlusionsValid_) updateOcclusions();

  for (uint32_t i = 0; i < sizex_; ++i) {
    for (uint32_t j = 0; j < sizey_; ++j) {
      for (uint32_t k = 0; k < sizez_; ++k) {
        // heuristic: find label from above.
        if (occlusions_[index(i, j, k)] != index(i, j, k)) {
          int32_t n = 1;
          while ((k + n < sizez_) && isOccluded(i, j, k + n) && voxels_[index(i, j, k + n)].count == 0) n += 1;
          if (k + n < sizez_ && voxels_[index(i, j, k + n)].count > 0) {
            int32_t gidx = index(i, j, k);
            if (voxels_[gidx].count == 0) occupied_.push_back(gidx);

            voxels_[gidx].count = voxels_[index(i, j, k + n)].count;
            voxels_[gidx].labels = voxels_[index(i, j, k + n)].labels;
          }
        }
      }
    }
  }
}

void VoxelGrid::updateOcclusions() {
  std::fill(occludedBy_.begin(), occludedBy_.end(), -2);
  std::fill(occlusions_.begin(), occlusions_.end(), -1);
  uint32_t occludedByCalls = 0;

  // move from outer to inner voxels.
  uint32_t num_shells = std::min<uint32_t>(sizex_, std::ceil(0.5 * sizey_));

  for (uint32_t o = 0; o < num_shells; ++o) {
    uint32_t i = sizex_ - o - 1;

    for (uint32_t j = 0; j < sizey_; ++j) {
      for (uint32_t k = 0; k < sizez_; ++k) {
        int32_t idx = index(i, j, k);
        if (occludedBy_[idx] == -2) {
          occludedByCalls += 1;
          occludedBy_[idx] = occludedBy(i, j, k);
        }
        occlusions_[idx] = occludedBy_[idx];
        //        if (occlusions_[idx] != idx) occluded_.push_back(idx);
      }
    }

    uint32_t j = o;
    for (uint32_t i = 0; i < sizex_ - o - 1; ++i) {
      for (uint32_t k = 0; k < sizez_; ++k) {
        int32_t idx = index(i, j, k);
        if (occludedBy_[idx] == -2) {
          occludedByCalls += 1;
          occludedBy_[idx] = occludedBy(i, j, k);
        }
        occlusions_[idx] = occludedBy_[idx];
        //          if (occlusions_[idx] != idx) occluded_.push_back(idx);
      }
    }

    j = sizey_ - o - 1;
    for (uint32_t i = 0; i < sizex_ - o - 1; ++i) {
      for (uint32_t k = 0; k < sizez_; ++k) {
        int32_t idx = index(i, j, k);
        if (occludedBy_[idx] == -2) {
          occludedByCalls += 1;
          occludedBy_[idx] = occludedBy(i, j, k);
        }
        occlusions_[idx] = occludedBy_[idx];
        //          if (occlusions_[idx] != idx) occluded_.push_back(idx);
      }
    }
  }

  // sanity check:
//  for (uint32_t i = 0; i < occludedBy_.size(); ++i) {
//    if (occludedBy_[i] == -2) std::cout << "occludedBy == -2" << std::endl;
//  }

  //  for (uint32_t i = 0; i < sizex_; ++i) {
  //    for (uint32_t j = 0; j < sizey_; ++j) {
  //      for (uint32_t k = 0; k < sizez_; ++k) {
  //        int32_t idx = index(i, j, k);
  //        if (occludedBy_[idx] == -2) {
  //          occludedByCalls += 1;
  //          occludedBy_[idx] = occludedBy(i, j, k);
  //        }
  //        occlusions_[idx] = occludedBy_[idx];
  //        if (occlusions_[idx] != idx) occluded_.push_back(idx);
  //      }
  //    }
  //  }

  //  std::cout << "occludedBy called " << occludedByCalls << " times." << std::endl;

  invalid_ = occlusions_;
  occlusionsValid_ = true;
}

void VoxelGrid::updateInvalid(const Eigen::Vector3f& position) {
  if (!occlusionsValid_) updateOcclusions();

  std::fill(occludedBy_.begin(), occludedBy_.end(), -2);

  // move from outer to inner voxels.
  uint32_t num_shells = std::min<uint32_t>(sizex_, std::ceil(0.5 * sizey_));

  for (uint32_t o = 0; o < num_shells; ++o) {
    uint32_t i = sizex_ - o - 1;

    for (uint32_t j = 0; j < sizey_; ++j) {
      for (uint32_t k = 0; k < sizez_; ++k) {
        int32_t idx = index(i, j, k);
        if (invalid_[idx] == -1) continue;  // just skip. invalid cannot be less then -1.

        if (occludedBy_[idx] == -2) {
          occludedBy_[idx] = occludedBy(i, j, k, position);
        }
        invalid_[idx] = std::min<int32_t>(invalid_[idx], occludedBy_[idx]);
      }
    }

    uint32_t j = o;
    for (uint32_t i = 0; i < sizex_ - o - 1; ++i) {
      for (uint32_t k = 0; k < sizez_; ++k) {
        int32_t idx = index(i, j, k);
        if (invalid_[idx] == -1) continue;  // just skip. invalid cannot be less then -1.

        if (occludedBy_[idx] == -2) {
          occludedBy_[idx] = occludedBy(i, j, k, position);
        }
        invalid_[idx] = std::min<int32_t>(invalid_[idx], occludedBy_[idx]);
      }
    }

    j = sizey_ - o - 1;
    for (uint32_t i = 0; i < sizex_ - o - 1; ++i) {
      for (uint32_t k = 0; k < sizez_; ++k) {
        int32_t idx = index(i, j, k);
        if (invalid_[idx] == -1) continue;  // just skip. invalid cannot be less then -1.

        if (occludedBy_[idx] == -2) {
          occludedBy_[idx] = occludedBy(i, j, k, position);
        }
        invalid_[idx] = std::min<int32_t>(invalid_[idx], occludedBy_[idx]);
      }
    }
  }

  //  for (uint32_t x = 0; x < sizex_; ++x) {
  //    for (uint32_t y = 0; y < sizey_; ++y) {
  //      for (uint32_t z = 0; z < sizez_; ++z) {
  //        int32_t idx = index(x, y, z);
  //        // idea: if voxel is not occluded, the value should be -1.
  //        if (occludedBy_[idx] == -2) occludedBy_[idx] = occludedBy(x, y, z, position);
  //        invalid_[idx] = std::min<int32_t>(invalid_[idx], occludedBy_[idx]);
  //      }
  //    }
  //  }
}

int32_t VoxelGrid::occludedBy(int32_t i, int32_t j, int32_t k, const Eigen::Vector3f& endpoint,
                              std::vector<Eigen::Vector3i>* visited) {
  float NextCrossingT[3], DeltaT[3]; /** t for next intersection with voxel boundary of axis, t increment for axis
  **/
  int32_t Step[3], Out[3], Pos[3];   /** voxel increment for axis, index of of outside voxels, current position **/
  float dir[3];                      /** ray direction **/

  if (visited != nullptr) visited->clear();

  Pos[0] = i;
  Pos[1] = j;
  Pos[2] = k;

  /** calculate direction vector assuming sensor at (0,0,_heightOffset) **/
  Eigen::Vector3f startpoint = voxel2position(Pos[0], Pos[1], Pos[2]);

  double halfResolution = 0.5 * resolution_;

  dir[0] = endpoint[0] - startpoint[0];
  dir[1] = endpoint[1] - startpoint[1];
  dir[2] = endpoint[2] - startpoint[2];

  /** initialize variables for traversal **/
  for (uint32_t axis = 0; axis < 3; ++axis) {
    if (dir[axis] < 0) {
      NextCrossingT[axis] = -halfResolution / dir[axis];
      DeltaT[axis] = -resolution_ / dir[axis];
      Step[axis] = -1;
      Out[axis] = 0;
    } else {
      NextCrossingT[axis] = halfResolution / dir[axis];
      DeltaT[axis] = resolution_ / dir[axis];
      Step[axis] = 1;
      Out[axis] = size(axis);
    }
  }

  Eigen::Vector3i endindexes = position2voxel(endpoint);
  int32_t i_end = endindexes[0];
  int32_t j_end = endindexes[1];
  int32_t k_end = endindexes[2];

  const int32_t cmpToAxis[8] = {2, 1, 2, 1, 2, 2, 0, 0};
  int32_t iteration = 0;

  std::vector<uint32_t> traversed;
  traversed.reserve(std::max(sizex_, std::max(sizey_, sizez_)));

  for (;;)  // loop infinitely...
  {
    if (Pos[0] < 0 || Pos[1] < 0 || Pos[2] < 0) break;
    if (Pos[0] >= int32_t(sizex_) || Pos[1] >= int32_t(sizey_) || Pos[2] >= int32_t(sizez_)) break;

    int32_t idx = index(Pos[0], Pos[1], Pos[2]);
    bool occupied = voxels_[idx].count > 0;
    if (visited != nullptr) visited->push_back(Eigen::Vector3i(Pos[0], Pos[1], Pos[2]));

    if (occupied) {
      for (auto i : traversed) occludedBy_[i] = idx;
      return idx;
    }

    traversed.push_back(idx);

    int32_t bits = ((NextCrossingT[0] < NextCrossingT[1]) << 2) + ((NextCrossingT[0] < NextCrossingT[2]) << 1) +
                   ((NextCrossingT[1] < NextCrossingT[2]));
    int32_t stepAxis = cmpToAxis[bits]; /* branch-free looping */

    Pos[stepAxis] += Step[stepAxis];
    NextCrossingT[stepAxis] += DeltaT[stepAxis];

    /** note the first condition should never happen, since we want to reach a point inside the grid **/
    if (Pos[stepAxis] == Out[stepAxis]) break;                         //... until outside, and leaving the loop here!
    if (Pos[0] == i_end && Pos[1] == j_end && Pos[2] == k_end) break;  // .. or the sensor origin is reached.

    ++iteration;
  }

  for (auto i : traversed) occludedBy_[i] = -1;

  return -1;
}
