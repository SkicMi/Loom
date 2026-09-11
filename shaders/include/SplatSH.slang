// Sferni harmonici: boja koja ovisi o smjeru pogleda.
//
// Ovo je blizanac SplatMath::colorFromSH iz Core/Splat.cpp i mora mu odgovarati do zadnjeg
// bita. Dva primjerka postoje jer jedan govori C++ a drugi Slang - a ne zato sto je netko
// zaboravio. Zato TEST USPOREDJUJE JEDAN S DRUGIM na pravim podacima: to je jedina obrana od
// toga da se s vremenom raziđu.
//
// RASPORED KOEFICIJENATA JE PO KANALIMA: prvih coeffsPerChannel su crveni, pa zeleni, pa plavi.
// Izmjereno korelacijom na pravoj sceni (0.84 po kanalima, 0.00 po koeficijentima).

float3 shColor(RWStructuredBuffer<float> rest, uint base, uint coeffsPerChannel,
               uint degree, float3 dc, float3 direction){
    const float SH0 = 0.28209479177387814;
    float3 result = float3(0.5, 0.5, 0.5) + SH0 * dc;

    if(degree == 0 || coeffsPerChannel == 0){
        return result;
    }

    const float length = sqrt(dot(direction, direction));
    if(length <= 0.0){
        return result;
    }
    const float3 d = direction / length;
    const float x = d.x, y = d.y, z = d.z;

    //Koeficijent k kanala c. Ovdje i nigdje drugdje zivi raspored po kanalima
    float3 sh0 = float3(rest[base + 0*coeffsPerChannel + 0], rest[base + 1*coeffsPerChannel + 0], rest[base + 2*coeffsPerChannel + 0]);
    float3 sh1 = float3(rest[base + 0*coeffsPerChannel + 1], rest[base + 1*coeffsPerChannel + 1], rest[base + 2*coeffsPerChannel + 1]);
    float3 sh2 = float3(rest[base + 0*coeffsPerChannel + 2], rest[base + 1*coeffsPerChannel + 2], rest[base + 2*coeffsPerChannel + 2]);

    const float c1 = 0.4886025119029199;
    result += -c1 * y * sh0 + c1 * z * sh1 - c1 * x * sh2;

    if(degree > 1){
        const float xx = x*x, yy = y*y, zz = z*z;
        const float xy = x*y, yz = y*z, xz = x*z;

        float3 s3 = float3(rest[base + 0*coeffsPerChannel + 3], rest[base + 1*coeffsPerChannel + 3], rest[base + 2*coeffsPerChannel + 3]);
        float3 s4 = float3(rest[base + 0*coeffsPerChannel + 4], rest[base + 1*coeffsPerChannel + 4], rest[base + 2*coeffsPerChannel + 4]);
        float3 s5 = float3(rest[base + 0*coeffsPerChannel + 5], rest[base + 1*coeffsPerChannel + 5], rest[base + 2*coeffsPerChannel + 5]);
        float3 s6 = float3(rest[base + 0*coeffsPerChannel + 6], rest[base + 1*coeffsPerChannel + 6], rest[base + 2*coeffsPerChannel + 6]);
        float3 s7 = float3(rest[base + 0*coeffsPerChannel + 7], rest[base + 1*coeffsPerChannel + 7], rest[base + 2*coeffsPerChannel + 7]);

        const float c20 = 1.0925484305920792;
        const float c22 = 0.31539156525252005;
        const float c24 = 0.5462742152960396;

        result += c20 * xy * s3
                - c20 * yz * s4
                + c22 * (2.0*zz - xx - yy) * s5
                - c20 * xz * s6
                + c24 * (xx - yy) * s7;

        if(degree > 2){
            float3 t8  = float3(rest[base + 0*coeffsPerChannel + 8],  rest[base + 1*coeffsPerChannel + 8],  rest[base + 2*coeffsPerChannel + 8]);
            float3 t9  = float3(rest[base + 0*coeffsPerChannel + 9],  rest[base + 1*coeffsPerChannel + 9],  rest[base + 2*coeffsPerChannel + 9]);
            float3 t10 = float3(rest[base + 0*coeffsPerChannel + 10], rest[base + 1*coeffsPerChannel + 10], rest[base + 2*coeffsPerChannel + 10]);
            float3 t11 = float3(rest[base + 0*coeffsPerChannel + 11], rest[base + 1*coeffsPerChannel + 11], rest[base + 2*coeffsPerChannel + 11]);
            float3 t12 = float3(rest[base + 0*coeffsPerChannel + 12], rest[base + 1*coeffsPerChannel + 12], rest[base + 2*coeffsPerChannel + 12]);
            float3 t13 = float3(rest[base + 0*coeffsPerChannel + 13], rest[base + 1*coeffsPerChannel + 13], rest[base + 2*coeffsPerChannel + 13]);
            float3 t14 = float3(rest[base + 0*coeffsPerChannel + 14], rest[base + 1*coeffsPerChannel + 14], rest[base + 2*coeffsPerChannel + 14]);

            const float c30 = -0.5900435899266435;
            const float c31 =  2.890611442640554;
            const float c32 = -0.4570457994644658;
            const float c33 =  0.3731763325901154;
            const float c35 =  1.445305721320277;

            result += c30 * y * (3.0*xx - yy) * t8
                    + c31 * xy * z * t9
                    + c32 * y * (4.0*zz - xx - yy) * t10
                    + c33 * z * (2.0*zz - 3.0*xx - 3.0*yy) * t11
                    + c32 * x * (4.0*zz - xx - yy) * t12
                    + c35 * z * (xx - yy) * t13
                    + c30 * x * (xx - 3.0*yy) * t14;
        }
    }

    return result;
}
