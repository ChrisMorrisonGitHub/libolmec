//
//  contact.h
//  libolmec
//
//  Created by Chris Morrison on 08/03/2013.
//  Copyright (c) 2013 Chris Morrison. All rights reserved.
//

#ifndef libolmec_contact_h
#define libolmec_contact_h

#include <time.h>

typedef struct _address_card
{
    char title[8];
    char **first_names;
    char *surname;
    char gender[8];
    time_t birthday;
    time_t aniversary;
    char *home_email;
    char *work_email;
} address_card_t;

#endif
