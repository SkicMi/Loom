#pragma once
#include <glm/glm.hpp>
#include <cstdint>

//Kamera opisana onako kako je opisuje fotografija, a ne grafika: zariste i glavna tocka u
//PIKSELIMA.
//
//To je oblik u kojem intrinsike dolaze iz kalibracije, iz EXIF-a i iz modela za procjenu
//dubine - i jedini u kojem se piksel da vratiti natrag u 3D. Projekcijska matrica zna isto,
//ali zapisano tako da se iz nje ne moze citati.
//
//Izvode se IZ MATRICE, ne pokraj nje. Da ih racunamo zasebno iz fovY i omjera stranica,
//dvije bi se formule s vremenom razisle i nista u kodu ne bi izgledalo krivo - scena bi samo
//ispala malo preplitka. Ovako ne mogu ne odgovarati onome cime je crtano.
struct CameraIntrinsics{
    float fx = 0.0f;   //zariste u pikselima, vodoravno
    float fy = 0.0f;   //okomito; NEGATIVNO kad projekcija okrece Y (Vulkan), i to je u redu
    float cx = 0.0f;   //glavna tocka u pikselima, od lijevog ruba
    float cy = 0.0f;   //od gornjeg ruba

    float nearPlane = 0.0f;
    float farPlane = 0.0f;

    //width i height su dimenzije slike u pikselima za koju ove intrinsike vrijede. Zariste u
    //pikselima ovisi o rezoluciji: ista kamera na pola rezolucije ima upola manji fx, a isto
    //vidno polje. Zato se ovo racuna pri ucitavanju, a ne sprema
    static CameraIntrinsics fromProjection(const glm::mat4& projection, uint32_t width, uint32_t height);

    //Vidno polje koje iz njih ispada, u radijanima. Tocno i kad glavna tocka nije u sredini,
    //jer se dvije polovice kuta racunaju odvojeno. Za provjeru i za ispis - stupanj je
    //citljiviji od piksela
    float horizontalFov(uint32_t width) const;
    float verticalFov(uint32_t height) const;

    bool isValid() const {return fx != 0.0f && fy != 0.0f;}
};
