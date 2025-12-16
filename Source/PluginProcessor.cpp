#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <cmath>

static inline float smoothStep01 (float x) noexcept
{
    x = juce::jlimit (0.0f, 1.0f, x);
    return x * x * (3.0f - 2.0f * x);
}

static inline float sin9Poly (float x) noexcept
{
    const float x2 = x * x;
    const float x3 = x2 * x;
    const float x5 = x3 * x2;
    const float x7 = x5 * x2;
    const float x9 = x7 * x2;
    return 9.0f * x - 120.0f * x3 + 432.0f * x5 - 576.0f * x7 + 256.0f * x9;
}


//==============================================================
// DSM@10% capture curve (derived from provided dry/DSM export pairs)
// Implemented as a fixed FIR magnitude curve.
//==============================================================
static const float kDsmCaptureTaps[1024] = {
    0.0f, 3.16026039e-10f, -1.97646077e-09f, 2.98121372e-09f, -8.27607671e-09f, 8.10514855e-09f,
    -1.92976994e-08f, 1.63621419e-08f, -3.3338285e-08f, 2.66917919e-08f, -5.1727941e-08f, 4.20990212e-08f,
    -7.16291524e-08f, 5.66364342e-08f, -1.0133607e-07f, 7.41279464e-08f, -1.3102445e-07f, 9.55594572e-08f,
    -1.63433072e-07f, 1.15164021e-07f, -1.98605633e-07f, 1.40358935e-07f, -2.38106878e-07f, 1.65743742e-07f,
    -2.78863837e-07f, 1.91854568e-07f, -3.22831028e-07f, 2.19156391e-07f, -3.65738828e-07f, 2.4803893e-07f,
    -4.1639197e-07f, 2.7961957e-07f, -4.70277172e-07f, 3.05060553e-07f, -5.16852879e-07f, 3.43216328e-07f,
    -5.67610471e-07f, 3.5775679e-07f, -6.11635528e-07f, 3.97077685e-07f, -6.64987226e-07f, 4.18750005e-07f,
    -7.06433923e-07f, 4.38618258e-07f, -7.57712996e-07f, 4.61355313e-07f, -8.04049023e-07f, 4.78148593e-07f,
    -8.30129125e-07f, 4.9282221e-07f, -8.80596417e-07f, 5.21156778e-07f, -9.32237356e-07f, 5.34185972e-07f,
    -9.66323341e-07f, 5.43722194e-07f, -9.9843362e-07f, 5.55526128e-07f, -1.04066817e-06f, 5.69704014e-07f,
    -1.06633672e-06f, 5.50345476e-07f, -1.08302618e-06f, 5.79886375e-07f, -1.13552755e-06f, 5.93693414e-07f,
    -1.13167061e-06f, 5.98688075e-07f, -1.19764024e-06f, 6.25875089e-07f, -1.22702863e-06f, 5.73410034e-07f,
    -1.16568378e-06f, 5.0103489e-07f, -1.39152928e-06f, 5.83076485e-07f, -1.45019351e-06f, 4.49750729e-07f,
    -1.43215948e-06f, 4.61255325e-07f, -1.48521053e-06f, 6.27582153e-07f, -1.2240572e-06f, 5.36253708e-07f,
    -1.37925406e-06f, 7.97144764e-07f, -1.62126798e-06f, 2.21650652e-07f, -1.26612008e-06f, 1.14119942e-07f,
    -1.59891249e-06f, 5.94732683e-07f, -1.6415438e-06f, 2.23179612e-07f, -1.6207423e-06f, 4.8927825e-07f,
    -1.6774037e-06f, 6.47002764e-07f, -1.76597234e-06f, 6.3515796e-07f, -1.63552954e-06f, 6.70253314e-07f,
    -2.27346436e-06f, 8.9377221e-07f, -1.75396167e-06f, 6.24030747e-07f, -2.0635789e-06f, 6.24484301e-07f,
    -2.31260333e-06f, 8.15881094e-07f, -2.50745256e-06f, 7.1313309e-07f, -2.30563819e-06f, 7.85275347e-07f,
    -2.7925737e-06f, 9.3495197e-07f, -2.95073437e-06f, 9.52411767e-07f, -2.96088615e-06f, 1.0740581e-06f,
    -3.30673902e-06f, 1.28711781e-06f, -3.55622637e-06f, 1.41793703e-06f, -3.73468151e-06f, 1.70049054e-06f,
    -4.25535245e-06f, 1.7947151e-06f, -4.21554296e-06f, 1.6490535e-06f, -4.37744575e-06f, 1.87872308e-06f,
    -5.27767543e-06f, 2.95501104e-06f, -5.49451079e-06f, 1.96135466e-06f, -5.46053752e-06f, 2.00266345e-06f,
    -6.97934593e-06f, 3.10500172e-06f, -7.59063505e-06f, 3.65873188e-06f, -6.98240547e-06f, 2.04236267e-06f,
    -7.2521575e-06f, 3.48668482e-06f, -8.66744085e-06f, 6.22260268e-06f, -1.05699828e-05f, 6.66468486e-06f,
    -8.40465873e-06f, 5.72946874e-06f, -8.42858753e-06f, 4.72406691e-06f, -1.07343485e-05f, 7.85806787e-06f,
    -1.26541272e-05f, 6.53336883e-06f, -1.26172235e-05f, 9.33489446e-06f, -8.64957565e-06f, 7.90796821e-06f,
    -1.10769943e-05f, 4.20821971e-06f, -1.17247237e-05f, 8.92539447e-06f, -1.74347661e-05f, 1.10990468e-05f,
    -1.86334673e-05f, 6.71916723e-06f, -1.79234557e-05f, 8.76524064e-06f, -1.78526898e-05f, 8.10384608e-06f,
    -1.5506912e-05f, 6.60608521e-06f, -2.03665895e-05f, 1.41520377e-05f, -2.58626897e-05f, 1.42620129e-05f,
    -2.52777554e-05f, 1.05879753e-05f, -2.27357068e-05f, 9.28274949e-06f, -2.12446084e-05f, 1.33030371e-05f,
    -2.48388023e-05f, 1.66285245e-05f, -2.83591107e-05f, 2.08496713e-05f, -2.81686425e-05f, 1.81952382e-05f,
    -3.37253441e-05f, 1.92408424e-05f, -3.33065254e-05f, 2.42225597e-05f, -3.49054571e-05f, 2.13771309e-05f,
    -3.6433823e-05f, 2.33731171e-05f, -3.71067472e-05f, 2.35299776e-05f, -3.81949903e-05f, 2.30479054e-05f,
    -4.23352685e-05f, 2.67938212e-05f, -4.54591318e-05f, 2.90148564e-05f, -4.54737565e-05f, 2.9117291e-05f,
    -4.58162649e-05f, 3.31926058e-05f, -4.87545476e-05f, 3.12206794e-05f, -4.89907943e-05f, 3.1261352e-05f,
    -4.89720769e-05f, 3.12954871e-05f, -5.09721722e-05f, 3.23684326e-05f, -5.28434211e-05f, 3.35656041e-05f,
    -5.61687957e-05f, 3.59276892e-05f, -5.83776236e-05f, 3.71123533e-05f, -6.05094065e-05f, 3.95796997e-05f,
    -6.23562591e-05f, 4.28350213e-05f, -6.64087202e-05f, 4.58502618e-05f, -6.44503816e-05f, 4.12170339e-05f,
    -6.338807e-05f, 3.94519593e-05f, -6.38487909e-05f, 4.20948199e-05f, -6.31753283e-05f, 3.81585669e-05f,
    -6.7951587e-05f, 3.97896001e-05f, -7.05202328e-05f, 4.40003932e-05f, -7.18350129e-05f, 4.68164544e-05f,
    -7.55042784e-05f, 4.52026143e-05f, -7.71212362e-05f, 4.40675612e-05f, -7.05033162e-05f, 4.35800175e-05f,
    -7.68336977e-05f, 4.64104196e-05f, -7.51068437e-05f, 4.80214076e-05f, -7.56689769e-05f, 4.46487393e-05f,
    -7.64296274e-05f, 5.01521026e-05f, -7.31833861e-05f, 4.07460902e-05f, -7.23950361e-05f, 4.51366359e-05f,
    -7.65387667e-05f, 4.82273208e-05f, -7.95992746e-05f, 5.07843979e-05f, -7.79693801e-05f, 4.6644047e-05f,
    -7.5882228e-05f, 4.2977339e-05f, -7.38895105e-05f, 4.48495048e-05f, -7.21020333e-05f, 4.0316645e-05f,
    -6.87781139e-05f, 4.09947243e-05f, -6.92434915e-05f, 4.08535452e-05f, -7.03022088e-05f, 3.79170851e-05f,
    -6.74794646e-05f, 3.57449535e-05f, -6.52502349e-05f, 3.35547738e-05f, -6.29645147e-05f, 3.08095914e-05f,
    -6.03031112e-05f, 2.96191793e-05f, -5.87316717e-05f, 2.6110547e-05f, -5.58745051e-05f, 2.60039669e-05f,
    -5.01441646e-05f, 2.36575343e-05f, -5.0342318e-05f, 2.18383702e-05f, -4.81464049e-05f, 1.46880438e-05f,
    -4.45628721e-05f, 1.07305477e-05f, -4.02870573e-05f, 9.59439603e-06f, -3.73221155e-05f, 8.94787627e-06f,
    -3.67711218e-05f, 3.18525281e-06f, -3.08087547e-05f, -1.01005537e-06f, -2.79430442e-05f, -3.16081446e-06f,
    -2.63644288e-05f, -8.84315523e-06f, -2.26406955e-05f, -9.85982479e-06f, -2.05979723e-05f, -1.28839592e-05f,
    -1.87058977e-05f, -1.76496123e-05f, -1.43717998e-05f, -2.1035492e-05f, -9.3424951e-06f, -2.11065999e-05f,
    -7.04857439e-06f, -2.3476332e-05f, -2.60357325e-07f, -2.87768289e-05f, -9.01599742e-06f, -3.40818879e-05f,
    -1.46027714e-05f, -4.34651847e-05f, -1.78013886e-06f, -3.71943679e-05f, -1.2178316e-05f, -3.584144e-05f,
    -2.37533295e-05f, -3.01554355e-05f, -5.10482096e-05f, -1.1822015e-05f, -4.95932327e-05f, -1.32343021e-05f,
    -3.20509935e-05f, -2.30014302e-05f, -3.32484233e-05f, -1.44017231e-05f, -4.90635939e-05f, 3.95542975e-06f,
    -6.66823471e-05f, -2.66858951e-05f, -1.24639146e-05f, -2.70950732e-05f, -5.60981562e-06f, 1.01867327e-05f,
    -4.36095179e-05f, 2.99004005e-05f, -3.63626823e-05f, -1.35437849e-05f, -6.92111062e-05f, 3.11356998e-05f,
    -7.04633494e-05f, 7.41104168e-06f, -0.00010194871f, 4.21538498e-05f, -7.04845224e-05f, -3.59172918e-05f,
    -0.000331265066f, -3.86890752e-05f, -0.000280760753f, 0.000144222577f, -0.000264539849f, 0.000143631449f,
    -0.00013568849f, 0.00017060648f, -0.000286644499f, 0.000180742616f, -0.000256281259f, 0.000308420305f,
    -0.000291557371f, 0.000215345062f, -0.000350949151f, 0.000254912215f, -0.000417381147f, 0.000257708423f,
    -0.000434150745f, 0.000363723084f, -0.000505370088f, 0.00034972094f, -0.000536420208f, 0.000456305279f,
    -0.0005460424f, 0.00053259515f, -0.000620232953f, 0.000493684609f, -0.000693528447f, 0.000550805591f,
    -0.000806911732f, 0.00059380976f, -0.000813818246f, 0.000663468381f, -0.000924330903f, 0.000607784314f,
    -0.000906971283f, 0.00061688904f, -0.00102794683f, 0.000702404301f, -0.00110928202f, 0.000742820033f,
    -0.00112264766f, 0.000795864209f, -0.0012029832f, 0.000873977318f, -0.00129742408f, 0.000893985445f,
    -0.00139966724f, 0.000955703144f, -0.00147094647f, 0.00100054953f, -0.00155565643f, 0.00103795866f,
    -0.00166757265f, 0.00107671181f, -0.00173930009f, 0.00113431388f, -0.00180881447f, 0.00108144968f,
    -0.00183319743f, 0.00141972688f, -0.00209205737f, 0.0014564821f, -0.00220345217f, 0.00140591781f,
    -0.00214915816f, 0.00133905828f, -0.00221257727f, 0.00156070571f, -0.00255856523f, 0.00164108025f,
    -0.00243414775f, 0.00176605734f, -0.00207453943f, 0.0018181866f, -0.00289782579f, 0.00183197984f,
    -0.00322170067f, 0.00150824641f, -0.00302754249f, 0.00183922739f, -0.00318150385f, 0.00193632091f,
    -0.00341269514f, 0.00157684262f, -0.00311430427f, 0.00176659191f, -0.00299573387f, 0.00212388812f,
    -0.00415478507f, 0.00240727374f, -0.0041272114f, 0.00161221321f, -0.00381850963f, 0.00125872937f,
    -0.0038007542f, 0.00171522528f, -0.0036207044f, 0.00266198022f, -0.00448019896f, 0.00257580285f,
    -0.00452005258f, 0.00225020759f, -0.00443998771f, 0.00248411763f, -0.00395993004f, 0.00216680393f,
    -0.00483015226f, 0.00292008347f, -0.00476999301f, 0.00365568209f, -0.00548314396f, 0.00309902127f,
    -0.00495011266f, 0.00282873958f, -0.00440413598f, 0.00255269022f, -0.00412010355f, 0.00370278396f,
    -0.00390578806f, 0.0028460063f, -0.0060935854f, 0.00299632619f, -0.00488904817f, 0.00418931479f,
    -0.00430911779f, 0.00378662185f, -0.00513107236f, 0.00364897167f, -0.00342690223f, 0.00532755256f,
    -0.00427678414f, 0.00847097673f, -0.00178567355f, 0.00723784557f, -0.000754562439f, -0.00199625106f,
    -0.0041760793f, 0.00362715218f, -0.00919874012f, -0.00552224508f, -0.0156900734f, 0.0032464473f,
    -0.0074251811f, -0.209691197f, 1.71998942f, -0.209687233f, -0.00742490124f, 0.0032462636f,
    -0.0156888887f, -0.00552172447f, -0.00919769891f, 0.00362667325f, -0.00417544879f, -0.00199591205f,
    -0.000754420122f, 0.00723634334f, -0.00178526924f, 0.00846889894f, -0.00427565398f, 0.00532604428f,
    -0.00342586753f, 0.00364780077f, -0.00512932893f, 0.00378526351f, -0.00430749031f, 0.00418765331f,
    -0.00488701696f, 0.00299502444f, -0.00609082263f, 0.00284466194f, -0.00390386907f, 0.0037008943f,
    -0.00411792286f, 0.00255129044f, -0.00440163724f, 0.00282708113f, -0.00494711613f, 0.00309708621f,
    -0.00547961565f, 0.00365326018f, -0.00476674177f, 0.00291803759f, -0.0048266761f, 0.00216520298f,
    -0.00395692838f, 0.002482187f, -0.00443645194f, 0.00224837265f, -0.00451627979f, 0.0025736033f,
    -0.00447628787f, 0.00265960488f, -0.0036174038f, 0.00171362865f, -0.00379714323f, 0.00125750911f,
    -0.00381473429f, 0.00161058793f, -0.00412297063f, 0.00240475358f, -0.00415035477f, 0.00212158239f,
    -0.00299242348f, 0.00176460529f, -0.0031107415f, 0.00157500792f, -0.00340865762f, 0.00193399226f,
    -0.00317761558f, 0.00183694356f, -0.00302372361f, 0.00150631426f, -0.00321751018f, 0.00182956096f,
    -0.00289394241f, 0.00181571406f, -0.00207167724f, 0.00176358572f, -0.00243069301f, 0.00163871853f,
    -0.00255483226f, 0.00155839755f, -0.00220926083f, 0.00133702438f, -0.0021458508f, 0.00140372617f,
    -0.00219997298f, 0.0014541531f, -0.00208867015f, 0.0014173995f, -0.00183015515f, 0.00107963313f,
    -0.00180573948f, 0.00113236252f, -0.00173627271f, 0.00107481575f, -0.00166460208f, 0.00103608845f,
    -0.0015528216f, 0.000998705742f, -0.00146820559f, 0.000953902665f, -0.00139700156f, 0.000892264361f,
    -0.0012948995f, 0.000872258504f, -0.00120059238f, 0.000794265943f, -0.00112036976f, 0.000741297263f,
    -0.0011069848f, 0.000700934906f, -0.00102577487f, 0.000615572615f, -0.000905016612f, 0.000606461603f,
    -0.000922299689f, 0.00066199631f, -0.000811995182f, 0.000592466909f, -0.000805069692f, 0.000549536373f,
    -0.000691915455f, 0.000492525811f, -0.000618763675f, 0.000531321915f, -0.000544725161f, 0.000455194619f,
    -0.000535102852f, 0.000348854432f, -0.000504106865f, 0.000362805935f, -0.000433046429f, 0.000257047213f,
    -0.000416301045f, 0.000254246901f, -0.000350025395f, 0.000214773419f, -0.000290776894f, 0.000307587761f,
    -0.000255583727f, 0.000180246599f, -0.000285851362f, 0.000170130545f, -0.000135306895f, 0.000143224228f,
    -0.00026378379f, 0.000143807076f, -0.000279945438f, -3.85758321e-05f, -0.000330287789f, -3.5810499e-05f,
    -7.02733087e-05f, 4.20265496e-05f, -0.000101638449f, 7.38831386e-06f, -7.02455945e-05f, 3.10387441e-05f,
    -6.89939479e-05f, -1.35009677e-05f, -3.624686e-05f, 2.98044451e-05f, -4.34685244e-05f, 1.01535525e-05f,
    -5.5914079e-06f, -2.70055098e-05f, -1.24224116e-05f, -2.65963845e-05f, -6.64570471e-05f, 3.94196832e-06f,
    -4.88954101e-05f, -1.43520001e-05f, -3.31328047e-05f, -2.29208745e-05f, -3.19379433e-05f, -1.31872903e-05f,
    -4.94158157e-05f, -1.17794234e-05f, -5.08630073e-05f, -3.00452648e-05f, -2.36659416e-05f, -3.57086647e-05f,
    -1.21328885e-05f, -3.70546622e-05f, -1.77340644e-06f, -4.32996712e-05f, -1.45467839e-05f, -3.3950324e-05f,
    -8.98095641e-06f, -2.8664228e-05f, -2.59331671e-07f, -2.33832252e-05f, -7.02043144e-06f, -2.10217622e-05f,
    -9.30469196e-06f, -2.09498048e-05f, -1.43128673e-05f, -1.75767582e-05f, -1.86281704e-05f, -1.28300699e-05f,
    -2.05112483e-05f, -9.81803987e-06f, -2.25441163e-05f, -8.80518473e-06f, -2.62504873e-05f, -3.14706494e-06f,
    -2.78207026e-05f, -1.00560442e-06f, -3.06721086e-05f, 3.17103422e-06f, -3.66059212e-05f, 8.9074174e-06f,
    -3.71522729e-05f, 9.55045471e-06f, -4.0101364e-05f, 1.0680772e-05f, -4.43548415e-05f, 1.46190387e-05f,
    -4.79187729e-05f, 2.17344641e-05f, -5.01012728e-05f, 2.35435418e-05f, -4.99010202e-05f, 2.58770815e-05f,
    -5.56001469e-05f, 2.59815315e-05f, -5.84396475e-05f, 2.94709844e-05f, -5.99995001e-05f, 3.06535039e-05f,
    -6.26435285e-05f, 3.33826429e-05f, -6.49134236e-05f, 3.55592965e-05f, -6.71267917e-05f, 3.77176875e-05f,
    -6.99302109e-05f, 4.06360268e-05f, -6.88725268e-05f, 4.07737389e-05f, -6.8405061e-05f, 4.00966164e-05f,
    -7.17061048e-05f, 4.46017038e-05f, -7.34787391e-05f, 4.27369414e-05f, -7.54551584e-05f, 4.63799151e-05f,
    -7.75251392e-05f, 5.0493265e-05f, -7.91401471e-05f, 4.79474329e-05f, -7.6091841e-05f, 4.48714527e-05f,
    -7.19670934e-05f, 4.05037499e-05f, -7.27454462e-05f, 4.98501431e-05f, -7.59666291e-05f, 4.43766039e-05f,
    -7.52049382e-05f, 4.77251087e-05f, -7.46405785e-05f, 4.61205309e-05f, -7.63508287e-05f, 4.33044552e-05f,
    -7.0054768e-05f, 4.3785476e-05f, -7.66245357e-05f, 4.49096988e-05f, -7.50119943e-05f, 4.65093326e-05f,
    -7.13608679e-05f, 4.37081835e-05f, -7.00490127e-05f, 3.9522085e-05f, -6.74919138e-05f, 3.78988443e-05f,
    -6.27426707e-05f, 4.18047493e-05f, -6.34060925e-05f, 3.9176728e-05f, -6.29431088e-05f, 4.09259155e-05f,
    -6.3992331e-05f, 4.55223817e-05f, -6.59308716e-05f, 4.252488e-05f, -6.19019629e-05f, 3.9289549e-05f,
    -6.00630519e-05f, 3.68368783e-05f, -5.79415937e-05f, 3.56576602e-05f, -5.57439889e-05f, 3.33101525e-05f,
    -5.24387251e-05f, 3.211898e-05f, -5.05768694e-05f, 3.10512478e-05f, -4.85874698e-05f, 3.10142823e-05f,
    -4.86011486e-05f, 3.09707902e-05f, -4.83618314e-05f, 3.29235372e-05f, -4.54424917e-05f, 2.88782303e-05f,
    -4.50980115e-05f, 2.87735711e-05f, -4.50786611e-05f, 2.65681247e-05f, -4.19763564e-05f, 2.28512454e-05f,
    -3.7866972e-05f, 2.33265873e-05f, -3.67839093e-05f, 2.31684389e-05f, -3.61126877e-05f, 2.11874722e-05f,
    -3.45937406e-05f, 2.40048194e-05f, -3.30051516e-05f, 1.90655883e-05f, -3.34161195e-05f, 1.80272982e-05f,
    -2.79069136e-05f, 2.0654652e-05f, -2.80920722e-05f, 1.64708927e-05f, -2.46017516e-05f, 1.31752204e-05f,
    -2.10391045e-05f, 9.19234481e-06f, -2.25127733e-05f, 1.04834453e-05f, -2.50264875e-05f, 1.41192686e-05f,
    -2.56020503e-05f, 1.40084285e-05f, -2.01584826e-05f, 6.53811276e-06f, -1.53462406e-05f, 8.01929036e-06f,
    -1.7665101e-05f, 8.67248855e-06f, -1.77324491e-05f, 6.64705249e-06f, -1.84320525e-05f, 1.09782131e-05f,
    -1.72435921e-05f, 8.82681888e-06f, -1.15942921e-05f, 4.16106468e-06f, -1.09519642e-05f, 7.81805375e-06f,
    -8.55050439e-06f, 9.227183e-06f, -1.24705566e-05f, 6.45685623e-06f, -1.25048246e-05f, 7.76465458e-06f,
    -1.06057796e-05f, 4.66705569e-06f, -8.32609385e-06f, 5.65926302e-06f, -8.30088084e-06f, 6.58175531e-06f,
    -1.04374376e-05f, 6.14396322e-06f, -8.55704548e-06f, 3.44192586e-06f, -7.15832311e-06f, 2.01572652e-06f,
    -6.8906138e-06f, 3.61024695e-06f, -7.48923185e-06f, 3.06318498e-06f, -6.8845834e-06f, 1.97524878e-06f,
    -5.38517043e-06f, 1.9340589e-06f, -5.4174061e-06f, 2.91319498e-06f, -5.20236063e-06f, 1.85168483e-06f,
    -4.31390799e-06f, 1.62491199e-06f, -4.15329487e-06f, 1.76798312e-06f, -4.19141406e-06f, 1.67471489e-06f,
    -3.67756957e-06f, 1.39605993e-06f, -3.50086475e-06f, 1.2668994e-06f, -3.25432279e-06f, 1.05687673e-06f,
    -2.91308447e-06f, 9.36892718e-07f, -2.90220373e-06f, 9.19429795e-07f, -2.74577064e-06f, 7.71988368e-07f,
    -2.26625048e-06f, 7.00832231e-07f, -2.4637784e-06f, 8.0153012e-07f, -2.27152168e-06f, 6.13279724e-07f,
    -2.02618003e-06f, 6.12606186e-07f, -1.72152124e-06f, 8.77070477e-07f, -2.23053735e-06f, 6.57464682e-07f,
    -1.6039919e-06f, 6.22779226e-07f, -1.73118326e-06f, 6.3411818e-07f, -1.64363212e-06f, 4.79318203e-07f,
    -1.58737987e-06f, 2.185336e-07f, -1.60698119e-06f, 5.82066264e-07f, -1.56446299e-06f, 1.11632261e-07f,
    -1.23819245e-06f, 2.16702887e-07f, -1.58463877e-06f, 7.78914341e-07f, -1.34732045e-06f, 5.23682502e-07f,
    -1.19499884e-06f, 6.12493125e-07f, -1.44903936e-06f, 4.49874705e-07f, -1.39635563e-06f, 4.38356324e-07f,
    -1.41295459e-06f, 5.67898326e-07f, -1.35480252e-06f, 4.87624902e-07f, -1.13403985e-06f, 5.57619273e-07f,
    -1.19274375e-06f, 6.08127834e-07f, -1.16316949e-06f, 5.81193774e-07f, -1.09809071e-06f, 5.75800527e-07f,
    -1.10076007e-06f, 5.61844672e-07f, -1.04877802e-06f, 5.32652336e-07f, -1.03147511e-06f, 5.50758614e-07f,
    -1.00545662e-06f, 5.36395817e-07f, -9.63430352e-07f, 5.24310167e-07f, -9.31178704e-07f, 5.14388262e-07f,
    -8.97017742e-07f, 5.01078944e-07f, -8.45988438e-07f, 4.73056701e-07f, -7.9613875e-07f, 4.58152385e-07f,
    -7.69690757e-07f, 4.41202047e-07f, -7.23861035e-07f, 4.18566572e-07f, -6.7337055e-07f, 3.98673876e-07f,
    -6.32310673e-07f, 3.77067124e-07f, -5.8000461e-07f, 3.38757701e-07f, -5.36634388e-07f, 3.23954481e-07f,
    -4.86999511e-07f, 2.86910819e-07f, -4.41431581e-07f, 2.61920832e-07f, -3.89167383e-07f, 2.31269198e-07f,
    -3.40139962e-07f, 2.03257088e-07f, -2.98523304e-07f, 1.76840416e-07f, -2.56147189e-07f, 1.51666015e-07f,
    -2.16982102e-07f, 1.27326103e-07f, -1.79263566e-07f, 1.03372535e-07f, -1.45794303e-07f, 8.46566053e-08f,
    -1.15169435e-07f, 6.45795666e-08f, -8.73839312e-08f, 4.82620557e-08f, -6.01927397e-08f, 3.47948763e-08f,
    -4.19021369e-08f, 2.1090937e-08f, -2.55258286e-08f, 1.20216566e-08f, -1.34016434e-08f, 5.18744203e-09f,
    -4.65539562e-09f, 1.32500466e-09f, -4.94119856e-10f, 0f
};

//==============================================================
// Parameter layout
//==============================================================
juce::AudioProcessorValueTreeState::ParameterLayout
FruityClipAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    // Left finger – input gain, in dB
    params.push_back (std::make_unique<juce::AudioParameterFloat>(
        "inputGain", "Input Gain",
        juce::NormalisableRange<float> (-12.0f, 12.0f, 0.01f), 0.0f));

    // FU#K (DSM capture) – 0..1
    params.push_back (std::make_unique<juce::AudioParameterFloat>(
        "ottAmount", "FU#K",
        juce::NormalisableRange<float> (0.0f, 1.0f), 0.0f));

    // MARRY (Silk) – 0..1
    params.push_back (std::make_unique<juce::AudioParameterFloat>(
        "silkAmount", "MARRY",
        juce::NormalisableRange<float> (0.0f, 1.0f), 0.0f));
// SAT – 0..1
    params.push_back (std::make_unique<juce::AudioParameterFloat>(
        "satAmount", "K#LL",
        juce::NormalisableRange<float> (0.0f, 1.0f), 0.0f));

    // MODE – 0 = clipper, 1 = limiter
    params.push_back (std::make_unique<juce::AudioParameterBool>(
        "useLimiter", "Use Limiter", false));

    params.push_back (std::make_unique<juce::AudioParameterChoice>(
        "clipMode", "Mode",
        juce::StringArray { "Digital", "Analog" }, 0));

    // OVERSAMPLE MODE – 0:x1, 1:x2, 2:x4, 3:x8, 4:x16, 5:x32, 6:x64
    params.push_back (std::make_unique<juce::AudioParameterChoice>(
        "oversampleMode", "Oversample Mode",
        juce::StringArray { "x1", "x2", "x4", "x8", "x16", "x32", "x64" }, 0));

    params.push_back (std::make_unique<juce::AudioParameterChoice>(
        "lookMode", "Look Mode",
        juce::StringArray { "COOKED", "LUFS", "STATIC" }, 0));

    return { params.begin(), params.end() };
}

//==============================================================
// Fruity-ish soft clip curve
// threshold: 0..1, where lower = earlier / softer onset
//==============================================================
float FruityClipAudioProcessor::fruitySoftClipSample (float x, float threshold)
{
    const float sign = (x >= 0.0f ? 1.0f : -1.0f);
    const float ax   = std::abs (x);

    if (ax <= threshold)
        return x;

    if (ax >= 1.0f)
        return sign * 1.0f;

    // Normalised smooth curve between threshold and 1.0
    const float t = (ax - threshold) / (1.0f - threshold); // 0..1
    const float shaped = threshold + (1.0f - (1.0f - t) * (1.0f - t)) * (1.0f - threshold);

    return sign * shaped;
}

//==============================================================
// Constructor / Destructor
//==============================================================
FruityClipAudioProcessor::FruityClipAudioProcessor()
    : juce::AudioProcessor (BusesProperties()
                                .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                                .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      parameters (*this, nullptr, "PARAMS", createParameterLayout())
{
    // postGain is no longer used for default hard-clip alignment.
    // We keep it as a member in case we want special modes later.
    postGain        = 1.0f;

    // Soft clip threshold (~ -6 dB at satAmount = 1)
    // (kept for future use; currently we are in pure hard-clip mode)
    thresholdLinear = juce::Decibels::decibelsToGain (-6.0f);

    juce::PropertiesFile::Options opts;
    opts.applicationName     = "GOREKLIPER";
    opts.filenameSuffix      = "settings";
    opts.osxLibrarySubFolder = "Application Support";
    opts.folderName          = "GOREKLIPER";

    userSettings = std::make_unique<juce::PropertiesFile> (opts);

    if (userSettings)
    {
        // ------------------------------------------------------
        // LOOK global default
        // ------------------------------------------------------
        if (! userSettings->containsKey ("lookMode"))
            userSettings->setValue ("lookMode", 0);  // 0 = Cooked

        const int storedLook = userSettings->getIntValue ("lookMode", 0);
        setStoredLookMode (storedLook);
        setLookModeIndex  (storedLook); // pushes into parameter for new instances

        // ------------------------------------------------------
        // OFFLINE oversample global default
        //   -1 = SAME (follow LIVE)
        // ------------------------------------------------------
        if (! userSettings->containsKey ("offlineOversampleIndex"))
            userSettings->setValue ("offlineOversampleIndex", -1);

        storedOfflineOversampleIndex =
            userSettings->getIntValue ("offlineOversampleIndex", -1);

        // ------------------------------------------------------
        // LIVE oversample global default
        // ------------------------------------------------------
        // We no longer store or restore a global LIVE oversample preference.
        // New instances simply use the default value of the "oversampleMode"
        // parameter (0 = 1x), and any changes are saved per-instance by the DAW.
        // Leave storedLiveOversampleIndex at its default of 0 so any legacy
        // code that reads it still sees "1x".
        storedLiveOversampleIndex = 0;
    }

    if (auto* choiceParam = dynamic_cast<juce::AudioParameterChoice*> (parameters.getParameter ("lookMode")))
    {
        const int storedIndex = juce::jlimit (0, choiceParam->choices.size() - 1, getStoredLookMode());
        setLookModeIndex (storedIndex);
    }
}

FruityClipAudioProcessor::~FruityClipAudioProcessor() = default;

FruityClipAudioProcessor::ClipMode FruityClipAudioProcessor::getClipMode() const
{
    if (auto* p = parameters.getRawParameterValue ("clipMode"))
    {
        const int idx = juce::jlimit (0, 1, (int) p->load());
        return idx == 0 ? ClipMode::Digital : ClipMode::Analog;
    }

    return ClipMode::Digital;
}

bool FruityClipAudioProcessor::isLimiterEnabled() const
{
    if (auto* p = parameters.getRawParameterValue ("useLimiter"))
        return p->load() >= 0.5f;

    return false;
}

int FruityClipAudioProcessor::getLookModeIndex() const
{
    if (auto* p = parameters.getRawParameterValue ("lookMode"))
        return (int) p->load();

    return 0;
}

void FruityClipAudioProcessor::setLookModeIndex (int newIndex)
{
    newIndex = juce::jlimit (0, 2, newIndex);

    if (auto* p = parameters.getRawParameterValue ("lookMode"))
        p->store ((float) newIndex);

    setStoredLookMode (newIndex);
}

int FruityClipAudioProcessor::getStoredLookMode() const
{
    if (userSettings)
        return userSettings->getIntValue ("lookMode", 0);
    return 0;
}

void FruityClipAudioProcessor::setStoredLookMode (int modeIndex)
{
    if (userSettings)
    {
        userSettings->setValue ("lookMode", modeIndex);
        userSettings->saveIfNeeded();
    }
}

int FruityClipAudioProcessor::getStoredOfflineOversampleIndex() const
{
    if (userSettings)
        return juce::jlimit (-1, 6,
            userSettings->getIntValue ("offlineOversampleIndex", storedOfflineOversampleIndex));

    return juce::jlimit (-1, 6, storedOfflineOversampleIndex);
}

void FruityClipAudioProcessor::setStoredOfflineOversampleIndex (int index)
{
    index = juce::jlimit (-1, 6, index);
    storedOfflineOversampleIndex = index;

    if (userSettings)
    {
        userSettings->setValue ("offlineOversampleIndex", index);
        userSettings->saveIfNeeded();
    }
}

int FruityClipAudioProcessor::getStoredLiveOversampleIndex() const
{
    return juce::jlimit (0, 6, storedLiveOversampleIndex);
}

void FruityClipAudioProcessor::setStoredLiveOversampleIndex (int index)
{
    index = juce::jlimit (0, 6, index);
    storedLiveOversampleIndex = index;
}

//==============================================================
// Oversampling config helper
//==============================================================
void FruityClipAudioProcessor::updateOversampling (int osIndex, int numChannels)
{
    // osIndex: 0=x1, 1=x2, 2=x4, 3=x8, 4=x16, 5=x32, 6=x64
    currentOversampleIndex = juce::jlimit (0, 6, osIndex);

    int numStages = 0; // factor = 2^stages
    switch (currentOversampleIndex)
    {
        case 0: numStages = 0; break; // x1 (no oversampling)
        case 1: numStages = 1; break; // x2
        case 2: numStages = 2; break; // x4
        case 3: numStages = 3; break; // x8
        case 4: numStages = 4; break; // x16
        case 5: numStages = 5; break; // x32
        case 6: numStages = 6; break; // x64
        default: numStages = 0; break;
    }

    if (numStages <= 0 || numChannels <= 0)
    {
        oversampler.reset();
        return;
    }

    oversampler = std::make_unique<juce::dsp::Oversampling<float>> (
        numChannels,
        numStages,
        juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR,
        true /* maximum quality */);

    oversampler->reset();

    if (maxBlockSize > 0)
        oversampler->initProcessing ((size_t) maxBlockSize);
}

//==============================================================
// Basic AudioProcessor overrides
//==============================================================
void FruityClipAudioProcessor::prepareToPlay (double newSampleRate, int samplesPerBlock)
{
    sampleRate   = (newSampleRate > 0.0 ? newSampleRate : 44100.0);
    limiterGain  = 1.0f;
    maxBlockSize = juce::jmax (1, samplesPerBlock);

    // ~50 ms release for limiter
    const float releaseTimeSec = 0.050f;
    limiterReleaseCo = std::exp (-1.0f / (releaseTimeSec * (float) sampleRate));

    // Reset K-weight filter + LUFS state
    resetKFilterState (getTotalNumOutputChannels());
    lufsMeanSquare = 1.0e-6f;
    lufsAverageLufs = -60.0f;
    // Reset SAT bass-tilt state
    resetSatState (getTotalNumOutputChannels());

    resetSilkState (getTotalNumOutputChannels());
    resetAnalogClipState (getTotalNumOutputChannels());
    resetAnalogTransientState (getTotalNumOutputChannels());

    //==========================================================
    // DSM capture FIR init (FU#K)
    //==========================================================
    {
        juce::dsp::ProcessSpec spec;
        spec.sampleRate = sampleRate;
        spec.maximumBlockSize = (juce::uint32) juce::jmax (1, samplesPerBlock);
        spec.numChannels = (juce::uint32) juce::jmax (1, getTotalNumOutputChannels());

        dsmCaptureFir.reset();
        dsmCaptureFir.state = juce::dsp::FIR::Coefficients<float>::make (kDsmCaptureTaps, dsmCaptureNumTaps);
        dsmCaptureFir.prepare (spec);

        // temp buffer used for the "wet" path of the captured curve
        dsmTemp.setSize ((int) spec.numChannels, (int) spec.maximumBlockSize, false, false, true);

        setLatencySamples (dsmCaptureLatency);
    }


    const float sr = (float) sampleRate;

    // DC tracker for quadratic even term in the 5060 (SILK) stage
    {
        constexpr float dcFc = 2.0f; // Hz (very low: remove drift, keep audio band)
        silkEvenDcAlpha = std::exp (-2.0f * juce::MathConstants<float>::pi * dcFc / sr);
        silkEvenDcAlpha = juce::jlimit (0.0f, 0.9999999f, silkEvenDcAlpha);
    }

    // Analog bias envelope follower coefficients (slow vs waveform, fast vs transients)
    {
        const float attackMs  = 1.5f;   // 1.5 ms attack
        const float releaseMs = 35.0f;  // 35 ms release
        const float aTau = attackMs  * 0.001f;
        const float rTau = releaseMs * 0.001f;
        analogEnvAttackAlpha  = std::exp (-1.0f / (aTau * sr));
        analogEnvReleaseAlpha = std::exp (-1.0f / (rTau * sr));
    }

    // One-pole lowpass for SAT bass tilt (around 300 Hz at base rate)
    {
        const float fcSat = 300.0f;
        const float alphaSat = std::exp (-2.0f * juce::MathConstants<float>::pi * fcSat / sr);
        satLowAlpha = juce::jlimit (0.0f, 1.0f, alphaSat);
    }

    // One-pole lowpasses for analog tone tilt splits (~250 Hz and ~10 kHz)
    {
        const float fcLow  = 250.0f;
        const float alphaL = std::exp (-2.0f * juce::MathConstants<float>::pi * fcLow / sr);
        analogToneAlpha250 = juce::jlimit (0.0f, 1.0f, alphaL);

        const float fcHigh = 10000.0f;
        const float alphaH = std::exp (-2.0f * juce::MathConstants<float>::pi * fcHigh / sr);
        analogToneAlpha10k = juce::jlimit (0.0f, 1.0f, alphaH);
    }

    // Transient envelope smoothing (fast/slow) for analog memory
    {
        const float fastTau = 0.0015f; // ~1.5 ms
        const float slowTau = 0.035f;  // ~35 ms
        analogFastEnvA = juce::jlimit (0.0f, 0.9999999f, std::exp (-1.0f / (fastTau * sr)));
        analogSlowEnvA = juce::jlimit (0.0f, 0.9999999f, std::exp (-1.0f / (slowTau * sr)));
    }

    // Slew limiter coefficient (~8 kHz corner)
    {
        const float alphaSlew = std::exp (-2.0f * juce::MathConstants<float>::pi * 8000.0f / sr);
        analogSlewA = juce::jlimit (0.0f, 0.9999999f, alphaSlew);
    }
    // Initial oversampling setup from parameter
    if (auto* osModeParam = parameters.getRawParameterValue ("oversampleMode"))
    {
        const int osIndex = (int) osModeParam->load();
        updateOversampling (osIndex, getTotalNumOutputChannels());
    }
    else
    {
        updateOversampling (0, getTotalNumOutputChannels());
    }

    // Reset GUI signal envelope for LUFS gating
    guiSignalEnv.store (0.0f);

    const int currentLookMode = getLookModeIndex();
    const int clampedLook     = juce::jlimit (0, 2, currentLookMode);

    if (clampedLook != currentLookMode)
        setLookModeIndex (clampedLook);
}

void FruityClipAudioProcessor::releaseResources() {}

bool FruityClipAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    auto main = layouts.getMainOutputChannelSet();
    return main == juce::AudioChannelSet::stereo()
        || main == juce::AudioChannelSet::mono();
}

//==============================================================
// K-weight filter reset
//==============================================================
void FruityClipAudioProcessor::resetKFilterState (int numChannels)
{
    kFilterStates.clear();
    if (numChannels <= 0)
        return;

    kFilterStates.resize ((size_t) numChannels);
    for (auto& st : kFilterStates)
    {
        st.z1a = st.z2a = 0.0f;
        st.z1b = st.z2b = 0.0f;
    }
}

void FruityClipAudioProcessor::resetSilkState (int numChannels)
{
    silkStates.clear();

    if (numChannels <= 0)
        return;

    silkStates.resize ((size_t) numChannels);

    for (auto& st : silkStates)
    {
        st.pre = 0.0f;
        st.de  = 0.0f;
        st.evenDc = 0.0f;
    }
}

//==============================================================
// SAT bass-tilt reset
//==============================================================
void FruityClipAudioProcessor::resetSatState (int numChannels)
{
    satStates.clear();
    if (numChannels <= 0)
        return;

    satStates.resize ((size_t) numChannels);
    for (auto& st : satStates)
        st.low = 0.0f;
}

//==============================================================
// Analog tone-match reset
//==============================================================
void FruityClipAudioProcessor::resetAnalogToneState (int numChannels)
{
    analogToneStates.clear();

    if (numChannels <= 0)
        return;

    analogToneStates.resize ((size_t) numChannels);
    for (auto& st : analogToneStates)
    {
        st.low250 = 0.0f;
        st.low10k = 0.0f;
    }
}

void FruityClipAudioProcessor::resetAnalogTransientState (int numChannels)
{
    analogTransientStates.clear();

    if (numChannels <= 0)
        return;

    analogTransientStates.resize ((size_t) numChannels);
    for (auto& st : analogTransientStates)
    {
        st.fastEnv = 0.0f;
        st.slowEnv = 0.0f;
        st.slew    = 0.0f;
    }
}

void FruityClipAudioProcessor::resetAnalogClipState (int numChannels)
{
    analogClipStates.clear();
    if (numChannels <= 0)
        return;

    analogClipStates.resize ((size_t) numChannels);
    for (auto& st : analogClipStates)
    {
        st.biasMemory = 0.0f;
        st.levelEnv   = 0.0f;
        st.dcBlock    = 0.0f;
    }
}


//==============================================================
// Limiter sample processor (0 lookahead, zero latency)
//==============================================================
float FruityClipAudioProcessor::processLimiterSample (float x)
{
    const float ax    = std::abs (x);
    const float limit = 1.0f;

    float desiredGain = 1.0f;
    if (ax > limit && ax > 0.0f)
        desiredGain = limit / ax;

    // Instant attack, exponential release
    if (desiredGain < limiterGain)
    {
        // Attack: clamp down immediately
        limiterGain = desiredGain;
    }
    else
    {
        // Release: move back towards 1.0
        limiterGain = limiterGain + (1.0f - limiterReleaseCo) * (desiredGain - limiterGain);
    }

    return x * limiterGain;
}

float FruityClipAudioProcessor::applySilkPreEmphasis (float x, int channel, float silkAmount)
{
    if (sampleRate <= 0.0)
        return x;

    auto& st = silkStates[(size_t) channel];

    // Shape the control for smoother response
    const float s   = juce::jlimit (0.0f, 1.0f, silkAmount);
    const float amt = std::pow (s, 0.8f);

    // One-pole lowpass around a few kHz to derive a "low" band
    const float fc    = juce::jmap (amt, 0.0f, 1.0f, 2400.0f, 6500.0f);
    const float alpha = std::exp (-2.0f * juce::MathConstants<float>::pi * fc / (float) sampleRate);

    st.pre = alpha * st.pre + (1.0f - alpha) * x;

    const float low  = st.pre;
    const float high = x - low;

    // Gentle HF tilt – starts at 0, tops out around +2–2.5 dB-ish
    const float tilt = juce::jmap (amt, 0.0f, 1.0f, 0.0f, 0.32f);

    return x + tilt * high;
}

float FruityClipAudioProcessor::applySilkDeEmphasis (float x, int channel, float silkAmount)
{
    if (sampleRate <= 0.0)
        return x;

    auto& st = silkStates[(size_t) channel];

    // Same shaped control
    const float s   = juce::jlimit (0.0f, 1.0f, silkAmount);
    const float amt = std::pow (s, 0.8f);

    // One-pole lowpass in the upper band to gently smooth top end
    const float fc    = juce::jmap (amt, 0.0f, 1.0f, 9500.0f, 6200.0f);
    const float alpha = std::exp (-2.0f * juce::MathConstants<float>::pi * fc / (float) sampleRate);

    st.de = alpha * st.de + (1.0f - alpha) * x;

    const float blend = juce::jmap (amt, 0.0f, 1.0f, 0.0f, 0.42f);

    return juce::jlimit (-2.5f, 2.5f, x + blend * (st.de - x));
}

float FruityClipAudioProcessor::applySilkAnalogSample (float x, int channel, float silkAmount)
{
    // 5060-style colour stage (pre-Lavry clip)
    //
    // Key fix:
    // On already-clipped / flat-topped material, (pre * pre) becomes mostly DC,
    // so the even-harmonic term collapses after DC removal. To keep even harmonics
    // alive on hot material, we square the LOW band from the pre-emphasis split.

    const float s = std::pow (juce::jlimit (0.0f, 1.0f, silkAmount), 0.8f);

    if (channel < 0 || channel >= (int) silkStates.size())
        return x;

    auto& st = silkStates[(size_t) channel];

    // Pre-emphasis (updates st.pre as the low-band state)
    const float pre = applySilkPreEmphasis (x, channel, s);

    // Engage more at high level so it doesn't fuzz quiet material
    float driveT = juce::jlimit (0.0f, 1.0f, (std::abs (pre) - 0.20f) / 0.80f);
    driveT = driveT * driveT;

    // Even-harmonic coefficient (tuned to hit hardware-like H2/H4/H6 on hot material)
    constexpr float evenScale = 2.7f; // was ~1.0
    float evenCoeff = evenScale * (0.035f + 0.0115f * s) * driveT;

    // IMPORTANT: build even term from low-band so it doesn't vanish on flat tops
    const float evenSrc = st.pre;
    float e = evenSrc * evenSrc;

    // Remove DC from quadratic term only (preserves even series)
    st.evenDc = silkEvenDcAlpha * st.evenDc + (1.0f - silkEvenDcAlpha) * e;
    e -= st.evenDc;

    // Raised cap so boost can actually take effect at hot levels
    const float evenCoeffCapped = juce::jlimit (0.0f, 0.40f, evenCoeff);

    float y = pre + evenCoeffCapped * e;

    // De-emphasis
    return applySilkDeEmphasis (y, channel, s);
}


float FruityClipAudioProcessor::applyClipperAnalogSample (float x, int channel, float silkAmount)
{
    constexpr float threshold = 1.0f;
    constexpr float baseKneeWidth = 0.38f;

    auto softClip = [] (float v, float kneeWidth) noexcept
    {
        constexpr float threshold = 1.0f;

        const float a = std::abs (v);
        if (a <= threshold)
            return v;

        const float over   = a - threshold;
        const float shaped = threshold + std::tanh (over / kneeWidth) * kneeWidth;
        return std::copysign (shaped, v);
    };

    // Shaped SILK control
    const float silkShape = std::pow (juce::jlimit (0.0f, 1.0f, silkAmount), 0.8f);

    // Per-channel state
    if (channel < 0 || channel >= (int) analogClipStates.size() || channel >= (int) analogTransientStates.size())
        return x;

    auto& st  = analogClipStates[(size_t) channel];
    auto& ts  = analogTransientStates[(size_t) channel];

    // Very gentle drive — we rely on bias & shape, not brute force
    const float baseDrive = 1.0f + 0.04f * silkShape;
    const float preEnv    = x * baseDrive;
    const float absPre    = std::abs (preEnv);

    // -------------------------------------------------------------
    // Fast/slow transient detector
    // -------------------------------------------------------------
    ts.fastEnv = analogFastEnvA * ts.fastEnv + (1.0f - analogFastEnvA) * absPre;
    ts.slowEnv = analogSlowEnvA * ts.slowEnv + (1.0f - analogSlowEnvA) * absPre;

    const float transient    = juce::jmax (0.0f, ts.fastEnv - ts.slowEnv);
    const float transientNorm = smoothStep01 (transient / 0.25f);

    const float dynamicKnee  = baseKneeWidth * (1.0f + 0.35f * transientNorm);
    const float dynamicDrive = baseDrive * (1.0f - 0.06f * transientNorm);

    float inRaw = x * dynamicDrive;

    // Optional slew blend during transients
    const float pre = inRaw;
    const float slewed = analogSlewA * ts.slew + (1.0f - analogSlewA) * pre;
    ts.slew = slewed;
    if (transientNorm > 0.0f)
        inRaw = slewed * (0.35f * transientNorm) + pre * (1.0f - 0.35f * transientNorm);

    // -------------------------------------------------------------
    // H9 harmonic fill (gated, strongest at SILK 0)
    // -------------------------------------------------------------
    const float xNorm = juce::jlimit (-1.0f, 1.0f, inRaw * 0.85f);
    const float absN  = std::abs (xNorm);
    const float gate  = smoothStep01 ((absN - 0.35f) / (0.95f - 0.35f));
    const float silkWeight = 1.0f - silkShape;
    const float h9Amt = 0.0014f * silkWeight * gate;
    inRaw += h9Amt * sin9Poly (xNorm);

    const float absIn = std::abs (inRaw);

    // -------------------------------------------------------------
    // Slow envelope follower of |in| (so bias doesn't "follow" the sine)
    // -------------------------------------------------------------
    float env = st.levelEnv;
    if (absIn > env)
        env = analogEnvAttackAlpha * env + (1.0f - analogEnvAttackAlpha) * absIn;
    else
        env = analogEnvReleaseAlpha * env + (1.0f - analogEnvReleaseAlpha) * absIn;

    st.levelEnv = env;

    // -------------------------------------------------------------
    // Bias envelope (engages near clipping)
    // -------------------------------------------------------------
    constexpr float levelStart = 0.55f; // start engaging below threshold
    constexpr float levelEnd   = 1.45f;

    float levelT = 0.0f;
    if (env > levelStart)
        levelT = juce::jlimit (0.0f, 1.0f, (env - levelStart) / (levelEnd - levelStart));

    // Baseline even content at SILK 0, more with SILK
    constexpr float biasBase = 0.018f;
    constexpr float biasSilk = 0.031f;

    float targetBias = (biasBase + biasSilk * silkShape) * levelT;

    // Micro "memory" on bias itself
    constexpr float biasAlpha = 0.992f; // ~4 ms @ 48k
    st.biasMemory = biasAlpha * st.biasMemory + (1.0f - biasAlpha) * targetBias;

    float bias = st.biasMemory;

    // Tame bias at insane levels (avoid fuzz)
    bias *= 1.0f / (1.0f + 0.20f * env);
    // -------------------------------------------------------------
    // Bias inside shaper (creates even harmonics)
    //
    // IMPORTANT:
    // We do NOT do (shaped - shaped(bias)) here anymore.
    // That DC-comp trick was killing the even-harmonic energy.
    // Instead, we allow the asymmetry to exist, then remove *only DC*
    // with an ultra-low cutoff one-pole HP (preserves H2/H4/H6).
    // -------------------------------------------------------------
    float y = softClip (inRaw + bias, dynamicKnee);

    // DC blocker (very low corner) – keeps the expensive even series, removes DC drift
    st.dcBlock = analogDcAlpha * st.dcBlock + (1.0f - analogDcAlpha) * y;
    y -= st.dcBlock;

    return juce::jlimit (-2.0f, 2.0f, y);
}

float FruityClipAudioProcessor::applyAnalogToneMatch (float x, int channel, float silkAmount)
{
    // Safety: bail out if we don't have a valid sample rate or state
    if (sampleRate <= 0.0)
        return x;

    if (channel < 0 || channel >= (int) analogToneStates.size())
        return x;

    auto& st = analogToneStates[(size_t) channel];

    // -----------------------------------------------------------------
    // 1) Split into three regions using two one-pole lowpasses:
    //    low  : below ~250 Hz
    //    mid  : 250 Hz – ~10 kHz
    //    high : above ~10 kHz
    // -----------------------------------------------------------------
    st.low250 = analogToneAlpha250 * st.low250 + (1.0f - analogToneAlpha250) * x;
    const float low = st.low250;

    st.low10k = analogToneAlpha10k * st.low10k + (1.0f - analogToneAlpha10k) * x;
    const float midPlusLow = st.low10k;

    const float mid  = midPlusLow - low;
    const float high = x - midPlusLow;

    // -----------------------------------------------------------------
    // 2) Read SILK amount and shape it
    //
    // rawSilk comes from the LOVE/SILK knob (0..1). We reuse the same
    // shaped control curve as the other silk code so the ear feels
    // consistent: most of the "movement" is towards the top of the knob.
    // -----------------------------------------------------------------
    const float s = std::pow (juce::jlimit (0.0f, 1.0f, silkAmount), 0.8f); // shaped SILK control

    // -----------------------------------------------------------------
    // 3) 3-band tilt target derived from measurements
    // -----------------------------------------------------------------
    const float lowDb  = juce::jmap (s, 0.0f, 1.0f, -0.28f, +0.37f);
    const float midDb  = juce::jmap (s, 0.0f, 1.0f, -0.31f, +0.45f);
    const float highDb = juce::jmap (s, 0.0f, 1.0f, -4.72f, -2.77f);
    const float gainLow  = juce::Decibels::decibelsToGain (lowDb);
    const float gainMid  = juce::Decibels::decibelsToGain (midDb);
    const float gainHigh = juce::Decibels::decibelsToGain (highDb);

    // -----------------------------------------------------------------
    // 4) Apply tilt and clamp
    // -----------------------------------------------------------------
    float y = gainLow * low + gainMid * mid + gainHigh * high;

    // Safety clamp – we should never normally hit this,
    // but it keeps the stage well-behaved in edge cases.
    return juce::jlimit (-4.0f, 4.0f, y);
}

//==============================================================
// CORE DSP
//==============================================================
void FruityClipAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                             juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numChannels = buffer.getNumChannels();
    const int numSamples  = buffer.getNumSamples();

    if ((int) kFilterStates.size() < numChannels)
        resetKFilterState (numChannels);
if ((int) satStates.size() < numChannels)
        resetSatState (numChannels);
    if ((int) silkStates.size() < numChannels)
        resetSilkState (numChannels);
    if ((int) analogToneStates.size() < numChannels)
        resetAnalogToneState (numChannels);
    if ((int) analogClipStates.size() < numChannels)
        resetAnalogClipState (numChannels);
    if ((int) analogTransientStates.size() < numChannels)
        resetAnalogTransientState (numChannels);

    const bool isOffline = isNonRealtime();

    auto* gainParam     = parameters.getRawParameterValue ("inputGain");
    auto* fuckParam     = parameters.getRawParameterValue ("ottAmount"); // FU#K knob (DSM capture)
    auto* silkParam     = parameters.getRawParameterValue ("silkAmount"); // MARRY knob (Silk)
    auto* satParam      = parameters.getRawParameterValue ("satAmount");
    auto* limiterParam  = parameters.getRawParameterValue ("useLimiter");
    auto* clipModeParam = parameters.getRawParameterValue ("clipMode");

    const float inputGainDb  = gainParam    ? gainParam->load()    : 0.0f;
    const float fuckRaw      = fuckParam    ? fuckParam->load()    : 0.0f;
    const float silkRaw      = silkParam    ? silkParam->load()    : 0.0f;
    const float satAmountRaw = satParam     ? satParam->load()     : 0.0f;
    const bool  useLimiter   = limiterParam ? (limiterParam->load() >= 0.5f) : false;

    const int clipModeIndex  = clipModeParam ? juce::jlimit (0, 1, (int) clipModeParam->load()) : 0;
    const ClipMode clipMode  = (clipModeIndex == 0 ? ClipMode::Digital : ClipMode::Analog);

    const float fuckAmount = juce::jlimit (0.0f, 1.0f, fuckRaw);
    const float silkAmount = juce::jlimit (0.0f, 1.0f, silkRaw);
    const float satAmount  = juce::jlimit (0.0f, 1.0f, satAmountRaw);

    const bool  isAnalogMode      = (clipMode == ClipMode::Analog);

    // Global scalars for this block
    // inputGain comes from the finger (in dB).
    const float inputGain = juce::Decibels::decibelsToGain (inputGainDb);

    // Coarse alignment (kept as 1.0f for now)
    constexpr float fruityCal = 1.0f;

    // Fine alignment scalar to tune RMS/null vs Fruity.
    // Start at 1.0f. Later you can try values like 0.99998f, 1.00002f, etc.
    constexpr float fruityFineCal = 0.99997f;

    // This is the actual drive into OTT/SAT/clipper for default mode.
    const float inputDrive = inputGain * fruityCal * fruityFineCal;

    // LIVE oversample index from parameter (0..6)
    int liveOsIndex = 0;
    if (auto* osModeParam = parameters.getRawParameterValue ("oversampleMode"))
        liveOsIndex = juce::jlimit (0, 6, (int) osModeParam->load());

    // Start from LIVE value
    int osIndex = liveOsIndex;

    // Offline override: -1 = SAME (follow live), 0..6 = explicit offline choice
    if (isOffline)
    {
        const int offlineIdx = getStoredOfflineOversampleIndex(); // -1..6
        if (offlineIdx >= 0)
            osIndex = offlineIdx;
    }

    // Make sure final index is in range 0..6 for updateOversampling
    osIndex = juce::jlimit (0, 6, osIndex);

    const bool bypassNow = gainBypass.load();
    if (bypassNow)
    {
        // BYPASS mode: apply only input gain (for loudness-matched A/B).
        for (int ch = 0; ch < numChannels; ++ch)
        {
            float* samples = buffer.getWritePointer (ch);

            for (int i = 0; i < numSamples; ++i)
            {
                samples[i] *= inputDrive;
            }
        }

        // IMPORTANT: we DO NOT return here anymore.
        // We still want to run the K-weighted meter and LUFS logic below,
        // so the LUFS label continues to move while bypassed.
    }
    else
    {
        // Oversampling mode can be changed at runtime – keep Oversampling object in sync
        if (osIndex != currentOversampleIndex || (! oversampler))
        {
            updateOversampling (osIndex, numChannels);
        }

        if (oversampler && maxBlockSize < numSamples)
        {
            maxBlockSize = numSamples;
            oversampler->initProcessing ((size_t) maxBlockSize);
        }

        //==========================================================
        //==========================================================
        // PRE-CHAIN: GAIN + FU#K (DSM capture) + MARRY (Silk)
        //   - always at base rate
        //==========================================================

        // 1) Apply input drive
        for (int ch = 0; ch < numChannels; ++ch)
        {
            float* samples = buffer.getWritePointer (ch);
            for (int i = 0; i < numSamples; ++i)
                samples[i] *= inputDrive;
        }

        // 2) FU#K: captured DSM curve (blend between dry and captured curve)
        if (fuckAmount > 0.0f && sampleRate > 0.0)
        {
            // Ensure temp buffer is large enough
            if (dsmTemp.getNumChannels() != numChannels || dsmTemp.getNumSamples() < numSamples)
                dsmTemp.setSize (numChannels, numSamples, false, false, true);

            dsmTemp.makeCopyOf (buffer, true);

            juce::dsp::AudioBlock<float> wetBlock (dsmTemp);
            juce::dsp::ProcessContextReplacing<float> wetCtx (wetBlock);
            dsmCaptureFir.process (wetCtx);

            // Blend: y = dry + a * (wet - dry)
            for (int ch = 0; ch < numChannels; ++ch)
            {
                float* dry = buffer.getWritePointer (ch);
                const float* wet = dsmTemp.getReadPointer (ch);

                for (int i = 0; i < numSamples; ++i)
                    dry[i] = dry[i] + fuckAmount * (wet[i] - dry[i]);
            }
        }

        // 3) MARRY: Silk in both modes; Analog tone-match only in Analog mode
        if (silkAmount > 0.0f || isAnalogMode)
        {
            for (int ch = 0; ch < numChannels; ++ch)
            {
                float* samples = buffer.getWritePointer (ch);

                for (int i = 0; i < numSamples; ++i)
                {
                    float s = samples[i];

                    if (silkAmount > 0.0f)
                        s = applySilkAnalogSample (s, ch, silkAmount);

                    if (isAnalogMode)
                        s = applyAnalogToneMatch (s, ch, silkAmount);

                    samples[i] = s;
                }
            }
        }

// SAT already applied previously if needed.
                    if (limiterOn)
                        sample = processLimiterSample (sample);
                    else if (isAnalogMode)
                        sample = applyClipperAnalogSample (sample, ch, silkAmountAnalog);
                    else
                    {
                        // Pure hard clip at base rate
                        if (sample >  1.0f) sample =  1.0f;
                        if (sample < -1.0f) sample = -1.0f;
                    }
samples[i] = sample;
                }
            }
        }

        // FINAL SAFETY CEILING AT BASE RATE
        // We ALWAYS end the chain with a strict Lavry-style 0 dBFS hard ceiling.
        // (Analog/Digital/OS/no-OS – doesn’t matter. Final output is guaranteed in [-1, +1].)
        for (int ch = 0; ch < numChannels; ++ch)
        {
            float* s = buffer.getWritePointer (ch);

            for (int i = 0; i < numSamples; ++i)
            {
                float y = s[i];
                if (y >  1.0f) y =  1.0f;
                if (y < -1.0f) y = -1.0f;
                s[i] = y;
            }
        }


        {
            const int numChannels = buffer.getNumChannels();
            const int numSamples  = buffer.getNumSamples();

            // We quantize to 24-bit domain: ±2^23 discrete steps.
            constexpr float quantSteps = 8388608.0f;       // 2^23
            constexpr float ditherAmp  = 1.0f / quantSteps; // ~ -138 dBFS

            // Simple random generator (LCG) per block.
            static uint32_t ditherState = 0x12345678u;
            auto randFloat = [&]() noexcept
            {
                ditherState = ditherState * 1664525u + 1013904223u;
                return (ditherState & 0x00FFFFFFu) / 16777216.0f;
            };

            for (int ch = 0; ch < numChannels; ++ch)
            {
                float* samples = buffer.getWritePointer (ch);

                for (int i = 0; i < numSamples; ++i)
                {
                    float s = samples[i];

                    // inaudible Fruity-style TPDF dither
                    const float r1 = randFloat();
                    const float r2 = randFloat();
                    const float tpdf = (r1 - r2) * ditherAmp;
                    s += tpdf;

                    // 24-bit style quantization
                    const float q = std::round (s * quantSteps) / quantSteps;

                    float y = q;
                    if (y >  1.0f) y =  1.0f;
                    if (y < -1.0f) y = -1.0f;
                    samples[i] = y;
                }
            }
        }
    }

    //==========================================================
    // METERING PASS (base rate, after distortion + final ceiling)
    //   - blockMax for burn + LUFS gate
    //   - K-weighted LUFS for GUI
    //==========================================================
    // K-weight filter coeffs (48 kHz reference; close enough)
    // Stage 1 (shelving) coefficients
    constexpr float k_b0a =  1.53512485958697f;
    constexpr float k_b1a = -2.69169618940638f;
    constexpr float k_b2a =  1.19839281085285f;
    constexpr float k_a1a = -1.69065929318241f;
    constexpr float k_a2a =  0.73248077421585f;

    // Stage 2 (RLB high-pass)
    constexpr float k_b0b =  1.0f;
    constexpr float k_b1b = -2.0f;
    constexpr float k_b2b =  1.0f;
    constexpr float k_a1b = -1.99004745483398f;
    constexpr float k_a2b =  0.99007225036621f;

    float  blockMax      = 0.0f;
    double sumSquaresK   = 0.0;
    const  int totalSamplesK = juce::jmax (1, numSamples * juce::jmax (1, numChannels));

    for (int ch = 0; ch < numChannels; ++ch)
    {
        float* samples = buffer.getWritePointer (ch);
        auto&  kf      = kFilterStates[(size_t) ch];

        for (int i = 0; i < numSamples; ++i)
        {
            float y = samples[i];

            // Track peak for GUI burn + gating
            const float ay = std::abs (y);
            if (ay > blockMax)
                blockMax = ay;

            // --- K-weighted meter path ---
            float xk = y;

            // Stage 1
            float v1 = xk - k_a1a * kf.z1a - k_a2a * kf.z2a;
            float y1 = k_b0a * v1 + k_b1a * kf.z1a + k_b2a * kf.z2a;
            kf.z2a = kf.z1a;
            kf.z1a = v1;

            // Stage 2
            float v2 = y1 - k_a1b * kf.z1b - k_a2b * kf.z2b;
            float y2 = k_b0b * v2 + k_b1b * kf.z1b + k_b2b * kf.z2b;
            kf.z2b = kf.z1b;
            kf.z1b = v2;

            sumSquaresK += (double) (y2 * y2);
        }
    }

    //==========================================================
    // Update GUI burn meter (0..1) from blockMax
    //==========================================================
    float normPeak = (blockMax - 0.90f) / 0.08f;   // 0.90 -> 0, 0.98 -> 1
    normPeak = juce::jlimit (0.0f, 1.0f, normPeak);
    normPeak = std::pow (normPeak, 2.5f);          // make mid-range calmer

    const float previousBurn = guiBurn.load();
    const float smoothedBurn = 0.25f * previousBurn + 0.75f * normPeak;
    const float burnForGui = bypassNow ? 0.0f : smoothedBurn;
    guiBurn.store (burnForGui);

    //==========================================================
    // Short-term LUFS (~1 s window for snappier meter)
    //   + signal gating envelope (for hiding the meter)
    //==========================================================
    if (sampleRate <= 0.0)
        sampleRate = 44100.0f;

    const float blockDurationSec = (float) numSamples / (float) sampleRate;

    // Exponential integrator approximating about a 1 s short-term window
    const float tauShortSec = 1.0f;
    float alphaMs = 0.0f;
    if (tauShortSec > 0.0f)
        alphaMs = 1.0f - std::exp (-blockDurationSec / tauShortSec);
    alphaMs = juce::jlimit (0.0f, 1.0f, alphaMs);

    float blockMs = 0.0f;
    if (totalSamplesK > 0 && sumSquaresK > 0.0)
        blockMs = (float) (sumSquaresK / (double) totalSamplesK);

    if (! std::isfinite (blockMs) || blockMs < 0.0f)
        blockMs = 0.0f;

    // Update short-term mean-square
    if (blockMs <= 0.0f)
    {
        // decay towards silence
        lufsMeanSquare *= (1.0f - alphaMs);
    }
    else
    {
        lufsMeanSquare = (1.0f - alphaMs) * lufsMeanSquare + alphaMs * blockMs;
    }

    if (lufsMeanSquare < 1.0e-12f)
        lufsMeanSquare = 1.0e-12f;

    // ITU-style: L = -0.691 + 10 * log10(z)
    float lufs = -0.691f + 10.0f * std::log10 (lufsMeanSquare);
    if (! std::isfinite (lufs))
        lufs = -60.0f;

    // --- Calibration offset to sit on top of MiniMeters short-term ---
    constexpr float lufsCalibrationOffset = 3.0f; // tweak if needed
    lufs += lufsCalibrationOffset;

    // clamp to a sane display range
    lufs = juce::jlimit (-60.0f, 6.0f, lufs);

    // --- Use the calibrated block energy for gate logic ---
    float blockLufs = -60.0f;
    if (blockMs > 0.0f)
    {
        float tmp = -0.691f + 10.0f * std::log10 (blockMs);
        if (std::isfinite (tmp))
            blockLufs = juce::jlimit (-80.0f, 6.0f, tmp + lufsCalibrationOffset);
    }

    // Treat as "has signal" if:
    //   - block short-term LUFS above ~ -60
    //   OR
    //   - raw peak above ~ -40 dBFS (0.01 linear)
    const bool hasSignalNow =
        (blockLufs > -60.0f) ||
        (blockMax > 0.01f);

    const float tauSeconds   = 2.0f;
    const float blockSeconds = (float) numSamples / (float) sampleRate;

    const float alphaAvg = juce::jlimit (0.0f, 1.0f,
                                         blockSeconds / (tauSeconds + blockSeconds));

    if (hasSignalNow)
        lufsAverageLufs = (1.0f - alphaAvg) * lufsAverageLufs + alphaAvg * blockLufs;

    float avgForBurn = lufsAverageLufs;

    float norm = 0.0f;
    if (avgForBurn <= -12.0f)
        norm = 0.0f;
    else if (avgForBurn >= -1.0f)
        norm = 1.0f;
    else
        norm = (avgForBurn + 12.0f) / 11.0f;

    const int numSteps = 11;
    int stepIndex = (int) std::floor (norm * (float) numSteps + 1.0e-6f);
    stepIndex = juce::jlimit (0, numSteps, stepIndex);

    const float steppedBurn = (float) stepIndex / (float) numSteps;
    const float targetBurnLufs = steppedBurn;

    // Smooth gate envelope so LUFS label doesn't flicker
    const float prevEnv   = guiSignalEnv.load();
    const float gateAlpha = 0.25f;
    const float targetEnv = hasSignalNow ? 1.0f : 0.0f;
    const float newEnv    = (1.0f - gateAlpha) * prevEnv + gateAlpha * targetEnv;
    guiSignalEnv.store (newEnv);

    const float burnEnv = newEnv;
    const float lufsBurnForGui = bypassNow ? 0.0f : (targetBurnLufs * burnEnv);
    guiBurnLufs.store (lufsBurnForGui);

    //==========================================================
    // GUI LUFS readout – DIRECT calibrated short-term value
    //==========================================================
    // At this point:
    //   - 'lufs' is already the calibrated short-term LUFS
    //     (includes the +3 dB offset so we sit on top of MiniMeters).
    //   - gating behaviour is handled by guiSignalEnv / getGuiHasSignal().
    //
    // We no longer add extra “mastering ballistics” on the NUMBER itself.
    // That way, our LUFS readout tracks Youlean/MiniMeters closely,
    // while the LOOK/BURN animation can stay lazy / vibey.
    guiLufs.store (lufs);
}

//==============================================================
// Editor
//==============================================================
juce::AudioProcessorEditor* FruityClipAudioProcessor::createEditor()
{
    return new FruityClipAudioProcessorEditor (*this);
}

//==============================================================
// Metadata
//==============================================================
const juce::String FruityClipAudioProcessor::getName() const      { return "GOREKLIPER"; }
bool FruityClipAudioProcessor::acceptsMidi() const                { return false; }
bool FruityClipAudioProcessor::producesMidi() const               { return false; }
bool FruityClipAudioProcessor::isMidiEffect() const               { return false; }
double FruityClipAudioProcessor::getTailLengthSeconds() const     { return 0.0; }

//==============================================================
// Programs
//==============================================================
int FruityClipAudioProcessor::getNumPrograms()                    { return 1; }
int FruityClipAudioProcessor::getCurrentProgram()                 { return 0; }
void FruityClipAudioProcessor::setCurrentProgram (int)            {}
const juce::String FruityClipAudioProcessor::getProgramName (int) { return {}; }
void FruityClipAudioProcessor::changeProgramName (int, const juce::String&) {}

//==============================================================
// State
//==============================================================
void FruityClipAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = parameters.copyState();
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void FruityClipAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
    {
        if (xml->hasTagName (parameters.state.getType()))
            parameters.replaceState (juce::ValueTree::fromXml (*xml));
    }
}

//==============================================================
// Entry point
//==============================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new FruityClipAudioProcessor();
}
