/**
 * @file car_sim_registry.c
 * @brief Registry of car simulators and the map "device parser number -> car simulator".
 *
 * @author Jan Lukaszewicz <brak@email.com>
 * @date 2026-10-27
 */

#include "car_sim_registry.h"
#include "car_sim_codec.h"
#include "sim_none.h"
#include <stdio.h>

/** @brief Function pointer type for retrieving a car simulator profile. */
typedef const car_sim_profile_t *(*car_sim_getter_fn)(void);

/** @brief Registry of the car simulator profile getters. */
static const car_sim_getter_fn s_sims[CAR_SIM_COUNT] = {
    [CAR_SIM_ID_NONE] = sim_none_profile_get,
};

const car_sim_profile_t *car_sim_get(car_sim_id_t id)
{
    if ((unsigned)id >= (unsigned)CAR_SIM_COUNT || s_sims[id] == NULL)
    {
        return NULL;
    }
    return s_sims[id]();
}

car_sim_id_t car_sim_find_by_dev_parser(uint16_t no)
{
    if (no == 0U)
    {
        return CAR_SIM_ID_NONE;
    }
    for (unsigned id = 0U; id < (unsigned)CAR_SIM_COUNT; id++)
    {
        const car_sim_profile_t *p = car_sim_get((car_sim_id_t)id);
        for (uint8_t i = 0U; (p != NULL) && (p->dev_parsers != NULL) && (i < p->num_dev_parsers); i++)
        {
            if (p->dev_parsers[i] == no)
            {
                return (car_sim_id_t)id;
            }
        }
    }
    return CAR_SIM_ID_NONE;
}

/** @brief Upper-case ASCII character. */
static char to_upper(char c)
{
    return (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c;
}

/** @brief Case-insensitive string comparison. */
static bool str_ieq(const char *a, const char *b)
{
    for (; *a != '\0' && *b != '\0'; a++, b++)
    {
        if (to_upper(*a) != to_upper(*b))
        {
            return false;
        }
    }
    return (*a == '\0') && (*b == '\0');
}

car_sim_id_t car_sim_find_by_key(const char *key)
{
    for (unsigned id = 0U; (key != NULL) && (id < (unsigned)CAR_SIM_COUNT); id++)
    {
        const car_sim_profile_t *p = car_sim_get((car_sim_id_t)id);
        if (p != NULL && p->key != NULL && str_ieq(key, p->key))
        {
            return (car_sim_id_t)id;
        }
    }
    return CAR_SIM_COUNT;
}

bool car_sim_registry_check(char *err, size_t err_size)
{
    if (err != NULL && err_size > 0U)
    {
        err[0] = '\0';
    }

    for (unsigned id = 0U; id < (unsigned)CAR_SIM_COUNT; id++)
    {
        const car_sim_profile_t *p = car_sim_get((car_sim_id_t)id);
        if (p == NULL)
        {
            if (err != NULL && err_size > 0U)
            {
                (void)snprintf(err, err_size, "symulator %u: brak profilu w rejestrze", id);
            }
            return false;
        }
        if (!car_sim_profile_check(p, err, err_size))
        {
            return false;
        }

        for (unsigned other = 0U; other < id; other++)
        {
            const car_sim_profile_t *q = car_sim_get((car_sim_id_t)other);
            if (str_ieq(p->key, q->key))
            {
                if (err != NULL && err_size > 0U)
                {
                    (void)snprintf(err, err_size, "%s: klucz powtórzony w symulatorze %u", p->key, other);
                }
                return false;
            }
        }

        for (uint8_t i = 0U; i < p->num_dev_parsers; i++)
        {
            const uint16_t no = p->dev_parsers[i];
            const car_sim_id_t owner = car_sim_find_by_dev_parser(no);
            if (no == 0U || owner != (car_sim_id_t)id)
            {
                if (err != NULL && err_size > 0U)
                {
                    (void)snprintf(err, err_size, "%s: parser urządzenia %u %s", p->key, (unsigned)no,
                                   (no == 0U) ? "zarezerwowany (brak parsera)" : "przypisany też do innego symulatora");
                }
                return false;
            }
        }
    }
    return true;
}
