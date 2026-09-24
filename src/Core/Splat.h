#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

//=============================================================================================
// One 3D gaussian, and the math that turns it into a blotch on the screen.
//
// No Vulkan and no Spool. Loom does not know where the gaussians came from - the file is
// someone else's business, just as Mesh does not know about .obj. The application is the one
// that copies what it read into a Splat.
//
// HERE THE VALUES ARE ALREADY ACTIVATED. The record on disk carries them in the form training
// optimized - logit, logarithm, unnormalized quaternion - because there they are unbounded, and
// an optimizer allowed to propose any number cannot work with a size that must stay positive.
// What is drawn derives from that form, and the boundary between the two is this file.
//=============================================================================================
struct Splat{
    glm::vec3 position{0.0f};
    glm::vec3 scale{1.0f};           //radii in scene units, already exp
    glm::quat rotation{1,0,0,0};     //already unit length
    float opacity = 1.0f;            //0..1, already sigmoid
    glm::vec3 color{1.0f};           //from degree 0 of the spherical harmonics
};

namespace SplatMath{

//-- activation ---------------------------------------------------------------------------------
//From what the record says into what gets drawn. Each of these four is one line of code and
//each can be forgotten without anything breaking - the scene just looks a little wrong

//Opacity is recorded as a logit because the sigmoid keeps the result in 0..1 by itself
float activateOpacity(float logit);

//Radius is recorded as a logarithm because exp cannot produce a negative number
glm::vec3 activateScale(const glm::vec3& logScale);

//A QUATERNION FROM THE RECORD IS NOT OF UNIT LENGTH. Measured on a real scene of 741883
//gaussians: lengths go from 0.414 to 1.908, average 0.954. Training normalizes them only at
//use, so they stay in the file as they came out. A rotation built from such a quaternion is
//not a rotation but a rotation plus scaling, and the covariance comes out up to twice too big.
//
//The order is the one from the record: rot_0 is W, then X, Y, Z
glm::quat activateRotation(const glm::vec4& stored);

//Degree 0 of the spherical harmonics is flat color, the same from all directions. The constant
//is the value of the zeroth SH basis; the half is an offset, because the coefficient may be
//negative
glm::vec3 colorFromSH0(const glm::vec3& dc);

//COLOR THAT DEPENDS ON THE VIEW DIRECTION. What degree 0 cannot do: a reflection on metal, a
//sky that changes, wet asphalt that is bright only from one angle. Training stores that in
//higher degrees of spherical harmonics - functions on the sphere, where each degree describes
//an ever finer change.
//
//THE COEFFICIENT LAYOUT IS PER CHANNEL, and that is not a matter of taste but of what the file
//says: the first 15 are red, then 15 green, then 15 blue. Measured on a real scene by
//correlating channels (the view effect is most often achromatic, so the same coefficient of
//different channels moves together): 0.84 per channel, 0.00 per coefficient. Whoever reads it
//backwards gets colors that change with angle wrongly - and that is NOT VISIBLE ON A SINGLE
//FRAME, only once the camera moves.
//
//   rest              one gaussian's coefficients, as Spool returns them
//   coeffsPerChannel  3 for degree 1, 8 for degree 2, 15 for degree 3
//   direction         from the camera TOWARD the gaussian, need not be unit
//
//The result is not clamped: a negative color is a legitimate intermediate result and gets
//clamped only when written into the image
glm::vec3 colorFromSH(const glm::vec3& dc,
                      const float* rest,
                      uint32_t coeffsPerChannel,
                      uint32_t degree,
                      const glm::vec3& direction);

//-- covariance ---------------------------------------------------------------------------------
//What a gaussian IS: an ellipsoid described by a 3x3 matrix. From radii and rotation, as
//R*S*S'*R'.
//
//Two things can be checked here without a single pixel, and the test checks them:
//   the matrix is symmetric, always
//   the determinant is (a*b*c)^2 for ANY rotation, because rotation does not change the volume.
//   That is the only check that catches an unnormalized quaternion, and it is the quietest
//   error here
glm::mat3 covariance3D(const glm::vec3& scale, const glm::quat& rotation);

//The same blotch seen through a camera: an ellipse on screen, a 2x2 matrix in pixels.
//
//Perspective is not linear, so an ellipsoid does not project into an ellipse exactly. A linear
//approximation is taken at the point where the gaussian is - the projection Jacobian - and that
//is EWA splatting. The approximation is the worse the farther the gaussian is from the axis,
//and that is why the ratio to the axis is clamped (see tangentLimit): without it the Jacobian
//at the frame edge grows and the blotch stretches across half the screen instead of vanishing.
//
//   view      matrix that puts a scene point into camera space
//   focalX/Y  focal lengths in PIXELS, the same thing CameraIntrinsics carries
//   blur      added to the diagonal, in pixels squared. A gaussian smaller than a pixel
//             otherwise disappears between samples and flickers across frames; 0.3 is the value
//             from the reference implementation. Zero means pure math, and that is how it was
//             tested
//
// Returns zero when the gaussian is behind the camera - there the projection makes no sense
// and the caller skips it
glm::mat2 covariance2D(const glm::mat3& covariance,
                       const glm::vec3& position,
                       const glm::mat4& view,
                       float focalX, float focalY,
                       float tangentLimitX, float tangentLimitY,
                       float blur = 0.3f);

//-- on screen -----------------------------------------------------------------------------------
//Where the gaussian's center lands in pixels. The same convention covariance2D uses, and that
//is no trifle: in Vulkan fy is NEGATIVE, because the projection flips Y (see CameraIntrinsics).
//Were the center projected with one convention and the covariance with another, the blotch
//would be in the right place but tilted the wrong way - an error visible only on oblique
//blotches, and barely at that
glm::vec2 projectToPixels(const glm::vec3& position,
                          const glm::mat4& view,
                          float focalX, float focalY,
                          float principalX, float principalY);

//A splat as it sits on the card between frames: everything already activated, because
//activation does not depend on the camera and is done once at load. Four float4s so std430 has
//nothing to align
struct RawSplat{
    glm::vec4 positionOpacity{0.0f};   //xyz position, w opacity
    glm::vec4 scale{0.0f};             //xyz radii, w unused
    glm::vec4 rotation{1,0,0,0};       //w,x,y,z of the quaternion - in that order, as the shader reads it
    glm::vec4 dc{0.0f};                //xyz degree-0 coefficients, raw
};

//Everything preparation needs that does not change within a frame. In a buffer and not a push
//constant, because the view matrix alone eats half of the guaranteed 128 bytes
struct PrepareParams{
    glm::mat4 view{1.0f};
    glm::vec4 cameraPosition{0.0f};
    glm::vec4 focalPrincipal{0.0f};   //fx, fy, cx, cy
    glm::vec4 limitsBlur{0.0f};       //x, y ratio limits; z blur
    glm::uvec4 counts{0};             //splatCount, shDegree, coeffsPerChannel
};

//What the rasterizer actually reads, and nothing more. Packed into three float4s so std430 then
//has nothing to align - the memory layout is the same on both sides without a single rule from
//memory
struct PreparedSplat{
    glm::vec4 centerConic{0.0f};   //xy center in pixels, zw the first two conic terms
    glm::vec4 conicOpacityDepth{0.0f}; //x third conic term, y opacity, z depth
    glm::vec4 color{0.0f};         //rgb color
};

//THE CONIC IS THE COVARIANCE INVERSE, which is why it is computed here and not in the shader:
//the rasterizer would otherwise have to invert the matrix for every pixel afresh, and the
//inverse depends only on the splat.
//
//Returns false when the splat has nothing to draw - it is behind the camera, or its ellipse is
//so thin that its determinant is zero and the inverse does not exist
bool prepare(const Splat& splat,
             const glm::mat4& view,
             float focalX, float focalY,
             float principalX, float principalY,
             float blur,
             PreparedSplat& out);

//CAN A PREPARED SPLAT DRAW EVEN ONE PIXEL OF TILE (tx, ty). False only when certainly not.
//
//A twin of splatTouchesTile from SplatTiles.slang, in the same order of operations - see there
//why looking at the tile's nearest point is enough. It exists so the CPU can independently
//count pairs, and so the test keeps both sides next to each other
bool touchesTile(const PreparedSplat& splat, int tileX, int tileY, uint32_t tileSize);

//-- selection ----------------------------------------------------------------------------------
//Whether a point is inside a rotated box.
//
//THE CENTER DECIDES, NOT THE EXTENT. A gaussian is a blotch without an edge - it extends into
//infinity, only ever more weakly - so "is it inside" has no unambiguous answer for it. With the
//center the answer reduces to a question that has one, and that is the question the user asks
//when boxing in a part of the scene with a cube: whatever center is inside, is inside.
//
//The cost of that shows at the box edge: a blotch whose center is just outside the edge stays,
//and its tail sticks in. For cleaning a scene that is the right compromise - a box that would
//delete everything intruding would also eat half the floor around itself
bool insideBox(const glm::vec3& point,
               const glm::vec3& center,
               const glm::vec3& halfExtent,
               const glm::quat& orientation);

//The same check for a box ALIGNED TO THE AXES. It exists for its cost, and the cost is
//measured - median of 15 passes over 3.76 million gaussians:
//
//   from the cloud, with a quaternion   14.5 ms
//   from the cloud, along the axes      11.3 ms
//   from a field of positions alone      3.9 ms
//
//The box the viewer uses to pick what to delete is axis aligned - rotation was only computed
//on it to be cancelled. And the rest is not compute but reading: a gaussian record is 62 bytes,
//so for 12 bytes of position it drags 233 MB through memory. Whoever calls this every frame,
//keep the positions in their own field - see SplatViewer
bool insideBox(const glm::vec3& point,
               const glm::vec3& center,
               const glm::vec3& halfExtent);

}
