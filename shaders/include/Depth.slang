//Depth from the depth buffer back into real units.
//
//The stored depth is nonlinear: almost all precision is spent near the camera. A threshold in
//metres over that value means nothing, and no point can be unprojected from it until it is
//put back.
//
//Inverse of ndcZ = far * (z - near) / ((far - near) * z), for Vulkan's depth range [0,1]:
//ndcZ = 0 returns near, ndcZ = 1 returns far.
//
//It lives in its own file because both the rate map fill and the unprojection need it. Two
//copies of the same formula are two chances to drift apart.
float linearizeDepth(float ndcZ, float nearPlane, float farPlane){
    float denominator = farPlane - ndcZ * (farPlane - nearPlane);
    if(denominator < 1e-6) return farPlane;
    return (farPlane * nearPlane) / denominator;
}
