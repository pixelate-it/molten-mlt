/*
 * factory.c -- registers the molten producer with MLT's service repository.
 *
 * Pattern matches src/modules/core and src/modules/kdenlive in MLT's own
 * source tree: MLT_REGISTER associates a service id ("molten") with its
 * constructor, MLT_REGISTER_METADATA associates it with the YAML describing
 * its parameters for host UIs.
 */

#include <framework/mlt.h>

#include <limits.h>
#include <stdio.h>

extern mlt_producer producer_molten_init(mlt_profile profile,
                                          mlt_service_type type,
                                          const char *id,
                                          char *arg);

static mlt_properties metadata(mlt_service_type type, const char *id, void *data)
{
    char file[PATH_MAX];
    snprintf(file, PATH_MAX, "%s/molten/%s", mlt_environment("MLT_DATA"), (char *) data);
    return mlt_properties_parse_yaml(file);
}

MLT_REPOSITORY
{
    MLT_REGISTER(mlt_service_producer_type, "molten", producer_molten_init);
    MLT_REGISTER_METADATA(mlt_service_producer_type, "molten", metadata, "producer_molten.yml");
}
