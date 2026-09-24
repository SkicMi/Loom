"""Floateri: gaussiani koji nisu dio scene, i kako ih naci bez ijednog praga u metrima.

STO SE MJERI. Svaki kadar iz snimke se nacrta, i za svaku gaussianu se zbroji koliko je piksela
stvarno obojila (tezina = alfa puta propusnost, zbrojena po pikselima). Zbroj se dobije jednim
unatraznim prolazom: boja svake gaussiane je 1, a gradijent zbroja slike po toj boji je bas
njezina tezina. Nista se ne procjenjuje - to je tocno ono sto rasterizacija radi.

DVIJE VRSTE SMECA, izmjereno na C0257 (3 631 276 gaussiana, 229 kadrova, pola 4K):

  NEVIDLJIVE   76.5 posto gaussiana ni u jednom kadru ne oboji ni pola piksela. Zajedno daju
               0.15 posto slike. MCMC svima dodaje sum to jaci sto su prozirnije, pa poluprozirne
               odlutaju po prostoru; kadar koji ih ne vidi ne vraca ih. Iz snimke se ne vide, a
               iz svakog drugog kuta su oblak oko scene. Bez njih: PSNR na kadrovima 25.680 ->
               25.678 dB, dakle nista - a splat je cetiri puta manji.

  UZ KAMERU    krupne (4-5 puta vece od medijana), dosta neprozirne (0.75) mrlje koje se vide u
               malo kadrova i ondje su puno blize kameri nego scena. Trening ih je stavio pred
               objektiv da popravi te kadrove; iz svakog drugog kuta vise u zraku.

OCJENA NA KADROVIMA KOJE TRENING NIJE VIDIO (C0257, 7000 koraka, izdvojeno 39 kadrova u odsjeccima
po tri, ciscenje mjereno samo na kadrovima treninga) - dakle na novim kutovima, gdje se floateri
i vide:

    sto se mice                            ostaje      PSNR medijan  prosjek  najgori
    nista                                  3 782 367   24.862        24.373   17.692
    nevidljive                               886 296   24.927        24.403   17.689
    + uz kameru, <=5 kadrova, <0.2 dubine    885 230   24.930        24.550   17.616
    + uz kameru, <=10 kadrova, <0.3 dubine   883 480   24.932        24.699   17.538   <- zadano
    + uz kameru, <=10 kadrova, <0.4 dubine   881 815   24.932        24.739   17.537
    + uz kameru, <=30 kadrova, <0.5 dubine   878 668   24.947        24.588   17.447

Zadano je blaze od najboljeg jer je mjereno na jednoj sceni; dalje od toga prosjek opet pada.

STO NE RADI (i zato nije ovdje): prazni prostor iz COLMAP-ovih opazanja - tocka vidjena u kadru
dokazuje da je zraka do nje prazna, pa je gaussiana na njoj visak. Nadje ~6000 gaussiana, ali
maknute ne mijenjaju nista vidljivo: jako su prozirne i leze ondje gdje ih ionako nista ne otkrije.
"""
import numpy as np
import torch
import gsplat


def view_depths(points, views):
    """Medijan dubine COLMAP-ovih tocaka koje kadar vidi: koliko je daleko scena iz tog kadra."""
    out = []
    for view in views:
        z = points @ view[:3, :3].T + view[:3, 3]
        z = z[:, 2]
        out.append(float(z[z > 0].median()) if bool((z > 0).any()) else 1.0)
    return out


def measure(means, quats, scales, opacities, views, K, width, height, depths, visible=0.5):
    """Po gaussiani: najveci broj piksela u jednom kadru, u koliko kadrova oboji barem 'visible'
    piksela, i najmanji omjer njezine dubine i dubine scene medju tim kadrovima.

    scales i opacities su AKTIVIRANI (exp, sigmoid); views su svijet-u-kameru 4x4 na kartici."""
    n = means.shape[0]
    device = means.device
    most = torch.zeros(n, device=device)
    seen = torch.zeros(n, dtype=torch.int32, device=device)
    nearest = torch.full((n,), float("inf"), device=device)
    ones = torch.ones(n, 1, device=device, requires_grad=True)
    means, quats, scales, opacities = (t.detach() for t in (means, quats, scales, opacities))
    for view, depth in zip(views, depths):
        drawn, _, _ = gsplat.rasterization(means, quats, scales, opacities, ones, view[None], K[None],
                                           width, height, sh_degree=None, packed=True)
        ones.grad = None
        drawn.sum().backward()
        weight = ones.grad[:, 0]
        significant = weight >= visible
        z = (means @ view[:3, :3].T + view[:3, 3])[:, 2]
        most = torch.maximum(most, weight)
        seen += significant.int()
        nearest = torch.minimum(nearest, torch.where(significant, z.clamp(min=0.0) / depth, torch.full_like(z, float("inf"))))
    return most, seen, nearest


def keep_mask(most, seen, nearest, visible=0.5, few_views=10, near=0.3):
    """True za gaussiane koje ostaju."""
    invisible = most < visible
    by_camera = (seen <= few_views) & (nearest < near)
    return ~(invisible | by_camera), int(invisible.sum()), int((by_camera & ~invisible).sum())
