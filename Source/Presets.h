// Onirique Shaper — les presets d'usine
//
// Chaque preset ne donne que ce qu'il change : le reste revient à sa valeur par défaut.
// Les identifiants sont ceux des paramètres (PluginProcessor.cpp).
#pragma once

#include <initializer_list>
#include <utility>

namespace onirique::presets
{

struct Factory
{
    const char* category;
    const char* name;
    std::initializer_list<std::pair<const char*, float>> values;
};

// Calés sur le moteur réglé comme spiff (sensitivity 7,5 par défaut, cut depth : 2 dB par cran).
// gain mode : 0 cut, 1 boost. band mode (1 à 3) : 0 low shelf, 1 parametric, 2 high shelf, 3 tilt shelf.
// band0 / band4 mode : pente 0 = 6, 1 = 12, 2 = 24, 3 = 48 dB/oct. window length : 0 short, 1 medium, 2 long.
inline const Factory factory[] = {
    // Divers
    { "DIVERS", "Par défaut", {} },
    { "DIVERS", "Départ à plat", { { "cut depth", 0.0f }, { "boost depth", 0.0f } } },
    { "DIVERS", "Écouter les transitoires", { { "cut depth", 10.0f }, { "delta", 1.0f } } },

    // Batterie
    { "BATTERIE", "Kick : attaque plus nette", {
        { "gain mode", 1.0f }, { "boost depth", 4.5f }, { "sensitivity", 7.5f }, { "sharpness", 3.0f },
        { "decay", 3.7f }, { "decay lf/hf", 4.0f },
        { "band1 mode", 0.0f }, { "band1 freq", 120.0f }, { "band1 sens", -10.0f },
        { "band3 freq", 4000.0f }, { "band3 sens", 6.0f }, { "band3 q", 0.8f }, { "phase mode", 1.0f } } },
    { "BATTERIE", "Caisse claire : claque adoucie", {
        { "cut depth", 7.5f }, { "sensitivity", 8.0f }, { "sharpness", 3.0f }, { "decay", 4.2f },
        { "band1 mode", 0.0f }, { "band1 freq", 220.0f }, { "band1 sens", -12.0f },
        { "band2 freq", 3500.0f }, { "band2 sens", 6.0f } } },
    { "BATTERIE", "Bus batterie : plus de punch", {
        { "gain mode", 1.0f }, { "boost depth", 3.5f }, { "sensitivity", 6.5f }, { "sharpness", 2.5f },
        { "decay", 5.2f }, { "decay lf/hf", 4.5f }, { "phase mode", 1.0f } } },
    { "BATTERIE", "Overheads : cymbales adoucies", {
        { "cut depth", 6.8f }, { "sensitivity", 8.5f }, { "decay", 5.2f },
        { "band0 on", 1.0f }, { "band0 freq", 2000.0f }, { "band0 mode", 2.0f },
        { "band3 mode", 2.0f }, { "band3 freq", 8000.0f }, { "band3 sens", 4.0f } } },
    { "BATTERIE", "Room : clics retirés", {
        { "cut depth", 10.0f }, { "sensitivity", 9.0f }, { "sharpness", 7.0f }, { "decay", 2.2f }, { "window length", 0.0f } } },
    { "BATTERIE", "Toms : plus de corps à l'attaque", {
        { "gain mode", 1.0f }, { "boost depth", 4.0f }, { "sharpness", 3.5f }, { "decay", 4.7f },
        { "band2 freq", 150.0f }, { "band2 sens", 8.0f }, { "band2 q", 0.9f },
        { "band3 mode", 2.0f }, { "band3 freq", 3000.0f }, { "band3 sens", -10.0f } } },

    // Voix
    { "VOIX", "Voix : bruits de bouche", {
        { "cut depth", 9.0f }, { "sensitivity", 8.0f }, { "sharpness", 8.0f }, { "decay", 1.0f }, { "window length", 0.0f },
        { "band0 on", 1.0f }, { "band0 freq", 3000.0f }, { "band0 mode", 2.0f } } },
    { "VOIX", "Voix : plosives", {
        { "cut depth", 9.0f }, { "sensitivity", 7.5f }, { "sharpness", 2.0f }, { "decay", 4.2f },
        { "band1 mode", 0.0f }, { "band1 freq", 120.0f }, { "band1 sens", 6.0f },
        { "band4 on", 1.0f }, { "band4 freq", 300.0f }, { "band4 mode", 2.0f } } },
    { "VOIX", "Voix : consonnes plus douces", {
        { "cut depth", 6.0f }, { "sensitivity", 7.5f }, { "decay", 3.2f },
        { "band0 on", 1.0f }, { "band0 freq", 1500.0f },
        { "band2 freq", 6000.0f }, { "band2 sens", 6.0f }, { "band2 q", 0.6f } } },
    { "VOIX", "Voix : plus d'articulation", {
        { "gain mode", 1.0f }, { "boost depth", 3.0f }, { "sensitivity", 6.5f }, { "sharpness", 4.0f }, { "decay", 3.2f },
        { "band2 freq", 3000.0f }, { "band2 sens", 6.0f }, { "phase mode", 1.0f } } },

    // Basse
    { "BASSE", "Basse : attaque du médiator", {
        { "gain mode", 1.0f }, { "boost depth", 4.0f }, { "sharpness", 4.0f }, { "decay", 3.7f },
        { "band1 mode", 0.0f }, { "band1 freq", 150.0f }, { "band1 sens", -12.0f },
        { "band2 freq", 1500.0f }, { "band2 sens", 8.0f }, { "band2 q", 0.6f }, { "phase mode", 1.0f } } },
    { "BASSE", "Basse : bruits de cordes", {
        { "cut depth", 7.5f }, { "sensitivity", 8.5f }, { "sharpness", 6.0f }, { "decay", 2.7f },
        { "band0 on", 1.0f }, { "band0 freq", 800.0f }, { "band0 mode", 2.0f } } },
    { "BASSE", "Basse synthé : attaque tenue", {
        { "cut depth", 5.2f }, { "sensitivity", 7.0f }, { "decay", 5.2f }, { "decay lf/hf", 3.0f }, { "window length", 2.0f } } },

    // Guitare
    { "GUITARE", "Acoustique : médiator adouci", {
        { "cut depth", 7.5f }, { "sensitivity", 8.5f }, { "sharpness", 6.0f }, { "decay", 3.2f },
        { "band2 freq", 3000.0f }, { "band2 sens", 6.0f } } },
    { "GUITARE", "Acoustique : plus de frappe", {
        { "gain mode", 1.0f }, { "boost depth", 3.5f }, { "sharpness", 4.0f }, { "decay", 3.7f },
        { "band2 freq", 2000.0f }, { "band2 sens", 5.0f }, { "phase mode", 1.0f } } },
    { "GUITARE", "Électrique : définition", {
        { "gain mode", 1.0f }, { "boost depth", 3.0f }, { "sensitivity", 7.0f }, { "sharpness", 4.0f }, { "decay", 4.2f },
        { "band1 mode", 0.0f }, { "band1 freq", 180.0f }, { "band1 sens", -8.0f }, { "phase mode", 1.0f } } },

    // Synthés et claviers
    { "SYNTHÉS ET CLAVIERS", "Piano : marteaux plus doux", {
        { "cut depth", 6.0f }, { "sensitivity", 7.5f }, { "sharpness", 8.0f }, { "decay", 5.2f },
        { "window length", 2.0f }, { "oversample", 1.0f } } },
    { "SYNTHÉS ET CLAVIERS", "Pluck plus net", {
        { "gain mode", 1.0f }, { "boost depth", 4.0f }, { "sharpness", 5.0f }, { "decay", 3.2f }, { "phase mode", 1.0f } } },
    { "SYNTHÉS ET CLAVIERS", "Pad : clics retirés", {
        { "cut depth", 9.0f }, { "sensitivity", 9.5f }, { "sharpness", 7.0f }, { "decay", 2.0f }, { "window length", 0.0f } } },

    // Image
    { "IMAGE", "Attaques ramenées au centre", {
        { "stereo mode", 1.0f }, { "stereo balance", 1.0f }, { "stereo link", 0.0f }, { "cut depth", 6.8f }, { "decay", 4.2f } } },
    { "IMAGE", "Attaques élargies", {
        { "stereo mode", 1.0f }, { "stereo balance", 1.0f }, { "stereo link", 0.0f },
        { "gain mode", 1.0f }, { "boost depth", 4.0f }, { "decay", 4.2f } } },
    { "IMAGE", "Centre plus percutant", {
        { "stereo mode", 1.0f }, { "stereo balance", 0.0f }, { "gain mode", 1.0f }, { "boost depth", 3.0f }, { "decay", 3.7f } } },

    // Mix : les usages de spiff (mettre en avant ou en arrière sans changer le volume, de l'expression, de la vie)
    { "MIX", "Instrument : plus d'expression", {
        { "gain mode", 1.0f }, { "boost depth", 7.0f }, { "sensitivity", 8.5f }, { "sharpness", 4.0f }, { "decay", 3.0f } } },
    { "MIX", "Mettre en avant", {
        { "gain mode", 1.0f }, { "boost depth", 7.0f }, { "sensitivity", 8.5f }, { "decay", 2.0f }, { "trim", -1.0f } } },
    { "MIX", "Mettre en arrière", {
        { "cut depth", 8.0f }, { "sensitivity", 8.5f }, { "decay", 2.0f }, { "trim", 1.0f } } },
    { "MIX", "Mix trop compressé : rendre la dynamique", {
        { "gain mode", 1.0f }, { "boost depth", 5.0f }, { "sensitivity", 6.5f }, { "sharpness", 2.5f }, { "decay", 5.2f },
        { "decay lf/hf", 4.5f }, { "oversample", 1.0f }, { "phase mode", 1.0f } } },

    // Master
    { "MASTER", "Transitoires domptées", {
        { "cut depth", 3.8f }, { "sensitivity", 6.5f }, { "decay", 4.2f },
        { "resolution", 1.0f }, { "oversample", 1.0f } } },
    { "MASTER", "Un peu plus de punch", {
        { "gain mode", 1.0f }, { "boost depth", 2.0f }, { "sensitivity", 6.5f }, { "decay", 4.2f }, { "decay lf/hf", 4.0f },
        { "oversample", 1.0f }, { "phase mode", 1.0f } } },
    { "MASTER", "Aigus plus doux", {
        { "cut depth", 4.5f }, { "sensitivity", 7.5f }, { "sharpness", 6.0f }, { "decay", 3.2f },
        { "band0 on", 1.0f }, { "band0 freq", 4000.0f }, { "band0 mode", 1.0f }, { "oversample", 1.0f } } },
};

} // namespace onirique::presets
