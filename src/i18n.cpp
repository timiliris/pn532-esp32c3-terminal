#include "i18n.h"
#include <stdarg.h>

static const char *const EN[STR_COUNT] = {
  "Starting", "Wi-Fi: ", "Tap a", "badge", "Pairing", "Code to enter:", "Place the badge", "Hold still...",
  "Badge detected", "NFC reader missing", "Check the wiring", "Wi-Fi setup", "Network: ", "Password: ",
  "%d bytes NDEF", "Not supported", "Wi-Fi lost", "Paired: %s", "Hold to reset: %ds", "Factory reset...", "Updating...",
  "Read", "NDEF write", "Raw write", "Erase",
  "Read complete", "Badge removed during the operation", "Unsupported card type",
  "NDEF: NTAG / Ultralight only", "Card is not NDEF formatted", "Could not read the badge",
  "Too long: %d bytes, %d available", "NDEF written (%d bytes)", "Write failed at page %d",
  "Write failed at block %d", "16 bytes max per block", "4 bytes max per page",
  "Block out of range (1..%d)", "Page outside the user area (4..%d)",
  "Block %d holds the sector keys: write refused", "Authentication refused (wrong key A?)",
  "Write failed", "Block %d written", "Page %d written", "Badge erased (%d pages)", "%d blocks erased",
  ", %d sectors protected by another key", "No badge presented in time", "Cancelled",
};

static const char *const FR[STR_COUNT] = {
  "Démarrage", "Wi-Fi : ", "Approchez", "un badge", "Appairage", "Code à saisir :", "Posez le badge", "Ne bougez pas...",
  "Badge détecté", "Lecteur RFID absent", "Vérifier le câblage", "Configuration Wi-Fi", "Réseau : ", "Mot de passe : ",
  "%d octets NDEF", "Non pris en charge", "Wi-Fi perdu", "Appairé : %s", "Réinitialisation : %ds", "Réinitialisation...", "Mise à jour...",
  "Lecture", "Écriture NDEF", "Écriture brute", "Effacement",
  "Lecture terminée", "Badge retiré pendant l'opération", "Type de carte non pris en charge",
  "NDEF : NTAG / Ultralight uniquement", "Carte non formatée NDEF", "Lecture du badge impossible",
  "Trop long : %d octets pour %d disponibles", "NDEF écrit (%d octets)", "Échec d'écriture page %d",
  "Échec d'écriture bloc %d", "16 octets max par bloc", "4 octets max par page",
  "Bloc hors limites (1..%d)", "Page hors zone utilisateur (4..%d)",
  "Le bloc %d contient les clés du secteur : écriture refusée", "Authentification refusée (clé A incorrecte ?)",
  "Échec d'écriture", "Bloc %d écrit", "Page %d écrite", "Badge effacé (%d pages)", "%d blocs effacés",
  ", %d secteurs protégés par une autre clé", "Aucun badge présenté à temps", "Annulé",
};

static const char *const *table = EN;

void setLang(const String &lang) { table = lang == "fr" ? FR : EN; }

const char *tr(Str id) { return id < STR_COUNT ? table[id] : ""; }

String trf(int id, ...) {
  char buf[160];
  va_list ap;
  va_start(ap, id);
  vsnprintf(buf, sizeof buf, tr((Str)id), ap);
  va_end(ap);
  return buf;
}
