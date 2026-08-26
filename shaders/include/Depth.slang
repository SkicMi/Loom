//Dubina iz depth buffera natrag u prave jedinice.
//
//Zapisana dubina je nelinearna: gotovo sva preciznost je potrosena blizu kamere. Prag u
//metrima nad tom vrijednoscu ne znaci nista, i nijedna tocka se iz nje ne da odprojicirati
//dok je se ne vrati.
//
//Obrat od ndcZ = far * (z - near) / ((far - near) * z), za Vulkanov raspon dubine [0,1]:
//ndcZ = 0 vraca near, ndcZ = 1 vraca far.
//
//Stoji u zasebnom fileu jer je trebaju i punjenje karte stope i odprojekcija. Dvije kopije
//iste formule su dvije prilike da se raziđu.
float linearizeDepth(float ndcZ, float nearPlane, float farPlane){
    float denominator = farPlane - ndcZ * (farPlane - nearPlane);
    if(denominator < 1e-6) return farPlane;
    return (farPlane * nearPlane) / denominator;
}
