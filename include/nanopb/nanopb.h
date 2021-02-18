#pragma

#include <map>
#include <vector>

#include "Eigen/Sparse"

namespace nanopb {
typedef Eigen::SparseMatrix<double> SpMat;
typedef Eigen::Triplet<double> Triplet;
typedef Eigen::VectorXd Vec;

// gamma correction constant.
static inline constexpr float DEFAULT_GAMMA = 2.2f;

class vec3 {
 private:
  float x, y, z;

 public:
  vec3(float x, float y, float z) {
    this->x = x;
    this->y = y;
    this->z = z;
  }
  vec3(float v) {
    this->x = v;
    this->y = v;
    this->z = v;
  }
  vec3() { this->x = this->y = this->z = 0; }
  vec3& operator+=(const vec3& b) {
    (*this) = (*this) + b;
    return (*this);
  }
  friend vec3 operator-(const vec3& a, const vec3& b) {
    return vec3(a.x - b.x, a.y - b.y, a.z - b.z);
  }
  friend vec3 operator+(const vec3& a, const vec3& b) {
    return vec3(a.x + b.x, a.y + b.y, a.z + b.z);
  }
  friend vec3 operator*(const float s, const vec3& a) {
    return vec3(s * a.x, s * a.y, s * a.z);
  }
  friend vec3 operator*(const vec3& a, const float s) { return s * a; }
  const float& operator[](int index) const { return ((float*)(this))[index]; }
  float& operator[](int index) { return ((float*)(this))[index]; }
};

inline float clamp(float x) {
  if (x > 1.0f) {
    return 1.0f;
  } else if (x < 0.0f) {
    return 0.0f;
  } else {
    return x;
  }
}

struct ImageData {
  std::vector<vec3> data;

  unsigned int width;
  unsigned int height;
};

// compute image gradient.
inline float vpq(float fpstar, float fqstar, float gp, float gq) {
  float fdiff = fpstar - fqstar;
  float gdiff = gp - gq;

  // equation (11) in the paper.
  return gdiff;

  // we can also mix gradients using equation (13) in the paper, as shown
  // below. but I didn't find the results that compelling, so I didn't
  // implement it in the final program
  /*
  if (fabs(fdiff) > fabs(gdiff)) {
          return fdiff;
  }
  else {
          return gdiff;
  }
  */
}

inline bool PoissonBlend(const ImageData& maskImage,
                         const ImageData& sourceImage,
                         const ImageData& targetImage, unsigned int mx,
                         unsigned int my, std::vector<unsigned char>& outImage,
                         float gamma = DEFAULT_GAMMA) {
  // we sanity check mx and my.
  {
    unsigned int xmin = mx;
    unsigned int ymin = my;

    unsigned int xmax = mx + maskImage.width;
    unsigned int ymax = my + maskImage.height;

    if (xmin > 0 && ymin > 0 && xmax < targetImage.width - 1 &&
        ymax < targetImage.height - 1) {
      // sanity check passed!
    } else {
      printf(
          "The specified source image(min = (%d,%d), max = (%d, %d)) does "
          "not "
          "fit in target image(%d,%d), max = (%d, %d))\n",
          xmin, ymin, xmax, ymax,

          1, 1, targetImage.height - 1, targetImage.height - 1);

      return false;
    }
  }

  // we represent the pixel coordinates in the target image as a single 1D
  // number, by flattening the (x,y) into a single index value. Every (x,y) will
  // have its own unique 1D coordinate.
  std::function<int(int, int)> targetFlatten =
      [&](unsigned int x, unsigned int y) { return targetImage.width * y + x; };

  std::function<unsigned int(unsigned int, unsigned int)> maskFlatten =
      [&](unsigned int x, unsigned int y) { return maskImage.width * y + x; };

  // check if pixel is part in mask. pixels with a red RGB value of 1.0 are part
  // of the mask. Note that we also have a small margin, though.
  std::function<bool(unsigned int, unsigned int)> isMaskPixel =
      [&](unsigned int x, unsigned int y) {
        return maskImage.data[maskFlatten(x, y)][0] > 0.99;
      };

  /*
  Every pixel involved in the poisson blending process, will have an unknown
  variable associated. The first pixel encountered in the mask is variable
  number 0, the second one is variable number 1, and so on.

  we use a std::map to associate pixel coordinates in the mask with variable
  numbers.
  */
  std::map<unsigned int, unsigned int> varMap;
  {
    int i = 0;
    for (unsigned int y = 0; y < maskImage.height; ++y) {
      for (unsigned int x = 0; x < maskImage.width; ++x) {
        if (isMaskPixel(x, y)) {
          varMap[maskFlatten(x, y)] = i;
          ++i;
        }
      }
    }
  }
  const unsigned int numUnknowns = (unsigned int)varMap.size();

  /*
  The poisson blending process involves solving a linear system Mx = b for the
  vector x.

  Below, we construct the matrix M. It is very sparse, so we only store the
  non-zero entries.
  */
  std::vector<Triplet> mt;  // M triplets. sparse matrix entries of M matrix.
  {
    unsigned int irow = 0;
    for (unsigned int y = my; y < my + maskImage.height; ++y) {
      for (unsigned int x = mx; x < mx + maskImage.width; ++x) {
        if (isMaskPixel(x - mx, y - my)) {
          /*
          Equation numbers are from the paper "Poisson Image Editing"
          http://www.cs.virginia.edu/~connelly/class/2014/comp_photo/proj2/poisson.pdf

          We use the left-hand-side of equation (7) for determining the values
          of M

          We do not allow poisson blending on the edges of the image, so we
          always have a value of 4 here.
          */
          mt.push_back(Triplet(irow, varMap[maskFlatten(x - mx, y - my)],
                               4));  // |N_p| = 4.

          /*
          The neighbouring pixels determine the next entries

          If a neighbouring pixel is not part of the mask, we must take the
      boundary condition into account, and this means that f_q ends up on the
      right-hand-side(because if it is a boundary condition, we already know
      the value of f_q, so it cannot be an unknown). So if a neighbour is
      outside the mask, no entry is pushed. Instead, it will be added to
      b(which we explain soon).
          */
          if (isMaskPixel(x - mx, y - my - 1)) {
            mt.push_back(
                Triplet(irow, varMap[maskFlatten(x - mx, y - 1 - my)], -1));
          }
          if (isMaskPixel(x - mx + 1, y - my)) {
            mt.push_back(
                Triplet(irow, varMap[maskFlatten(x - mx + 1, y - my)], -1));
          }
          if (isMaskPixel(x - mx, y - my + 1)) {
            mt.push_back(
                Triplet(irow, varMap[maskFlatten(x - mx, y - my + 1)], -1));
          }
          if (isMaskPixel(x - mx - 1, y - my)) {
            mt.push_back(
                Triplet(irow, varMap[maskFlatten(x - mx - 1, y - my)], -1));
          }

          ++irow;  // jump to the next row in the matrix.
        }
      }
    }
  }

  // we will use M to solve for x three times in a row.
  // we found that a simple Cholesky decomposition gave us good, and fast
  // results.
  Eigen::SimplicialCholesky<SpMat> solver;
  {
    SpMat mat(numUnknowns, numUnknowns);
    mat.setFromTriplets(mt.begin(), mt.end());
    solver.compute(mat);
  }

  Vec solutionChannels[3];
  Vec b(numUnknowns);

  for (unsigned int ic = 0; ic < 3; ++ic) {
    /*
    For each of the three color channels RGB, there will be a different b
    vector.

    So to perform poisson blending on the entire image, we must solve for x
    three times in a row, one time for each channel.

    */

    unsigned int irow = 0;

    for (unsigned int y = my; y < my + maskImage.height; ++y) {
      for (unsigned int x = mx; x < mx + maskImage.width; ++x) {
        if (isMaskPixel(x - mx, y - my)) {
          // we only ended up using v in the end.
          vec3 v = sourceImage.data[maskFlatten(x - mx, y - my)];
          vec3 u = targetImage.data[targetFlatten(x, y)];

          /*
          The right-hand side of (7) determines the value of b.

          below, we sum up all the values of v_pq(the gradient) for all
          neighbours.
          */
          float grad =
              vpq(u[ic],
                  targetImage.data[targetFlatten(x, y - 1)][ic],  // unused
                  v[ic],
                  sourceImage
                      .data[maskFlatten(x - mx, y - 1 - my)][ic])  // used
              + vpq(u[ic],
                    targetImage.data[targetFlatten(x - 1, y)][ic],  // unused
                    v[ic],
                    sourceImage
                        .data[maskFlatten(x - 1 - mx, y - my)][ic])  // used
              +
              vpq(u[ic],
                  targetImage.data[targetFlatten(x, y + 1)][ic],  // unused
                  v[ic],
                  sourceImage.data[maskFlatten(x - mx, y + 1 - my)][ic]  // used
                  ) +
              vpq(u[ic],
                  targetImage.data[targetFlatten(x + 1, y)][ic],  // unused
                  v[ic],
                  sourceImage
                      .data[maskFlatten(x + 1 - mx, y - my)][ic]);  // used

          b[irow] = grad;

          /*
          Finally, due to the boundary condition, some values of f_q end up on
          the right-hand-side, because they are not unknown.

          The ones outside the mask end up here.
          */
          if (!isMaskPixel(x - mx, y - my - 1)) {
            b[irow] += targetImage.data[targetFlatten(x, y - 1)][ic];
          }
          if (!isMaskPixel(x - mx + 1, y - my)) {
            b[irow] += targetImage.data[targetFlatten(x + 1, y)][ic];
          }
          if (!isMaskPixel(x - mx, y - my + 1)) {
            b[irow] += targetImage.data[targetFlatten(x, y + 1)][ic];
          }
          if (!isMaskPixel(x - mx - 1, y - my)) {
            b[irow] += targetImage.data[targetFlatten(x - 1, y)][ic];
          }

          ++irow;
        }
      }
    }

    // solve for channel number ic.
    solutionChannels[ic] = solver.solve(b);
  }

  // finally, we output the poisson blended image.
  {
    // first, output the original image to outImage
    for (unsigned int i = 0; i < targetImage.data.size(); ++i) {
      vec3 v = targetImage.data[i];
      outImage.push_back((unsigned char)(pow(v[0], gamma) * 255.0f));
      outImage.push_back((unsigned char)(pow(v[1], gamma) * 255.0f));
      outImage.push_back((unsigned char)(pow(v[2], gamma) * 255.0f));
      outImage.push_back(255);
    }

    // now modify outImage, to include the poisson blended pixels.
    for (unsigned int y = my; y < my + maskImage.height; ++y) {
      for (unsigned int x = mx; x < mx + maskImage.width; ++x) {
        if (isMaskPixel(x - mx, y - my)) {
          unsigned int i = varMap[maskFlatten(x - mx, y - my)];
          vec3 col =
              vec3((float)solutionChannels[0][i], (float)solutionChannels[1][i],
                   (float)solutionChannels[2][i]);

          col[0] = clamp(col[0]);
          col[1] = clamp(col[1]);
          col[2] = clamp(col[2]);

          outImage[4 * targetFlatten(x, y) + 0] =
              (unsigned char)(pow(col[0], gamma) * 255.0f);
          outImage[4 * targetFlatten(x, y) + 1] =
              (unsigned char)(pow(col[1], gamma) * 255.0f);
          outImage[4 * targetFlatten(x, y) + 2] =
              (unsigned char)(pow(col[2], gamma) * 255.0f);
        }
      }
    }

    return true;
  }
}
};  // namespace nanopd