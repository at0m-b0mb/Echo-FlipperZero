#pragma once

#include <gui/view.h>

#include "../helpers/echo_db.h"

#define DOSSIER_MAX_SSIDS 8
#define DOSSIER_PAGES     3

typedef struct DossierView DossierView;

DossierView* dossier_view_alloc(void);
void dossier_view_free(DossierView* view);
View* dossier_view_get_view(DossierView* view);

void dossier_view_update(
    DossierView* view,
    const EchoDevice* device,
    const ExpoResult* result,
    const EchoSsid* ssids,
    size_t ssid_count,
    uint8_t group_size);

/** Put the card back on page one. Called when the scene opens. */
void dossier_view_reset(DossierView* view);
