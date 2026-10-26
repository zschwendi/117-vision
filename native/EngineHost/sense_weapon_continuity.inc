/* Orientation stages of the accepted primary pose, applied exactly once per
 * outer epoch at admission (sense_weapon_frame.inc): grip latch/release
 * continuity, then optional scoped-aim smoothing. Both run at the frozen
 * epoch time on the frozen leased pose; with their settings off the pose
 * passes through bit for bit. */
#include "sense_grip_continuity.h"
#include "sense_scope_smoothing.h"
static HaloSenseGripContinuity sense_grip;
static HaloSenseScopeFilter sense_scope_aim;
static int sense_action_for_unit(uint32_t,uint32_t,uint32_t,int *);
static void sense_accept_orientation(uint32_t unit,uint32_t datum,uint32_t weapon,const HaloSenseDualInput *input,
                                     uint64_t epoch,uint64_t epoch_ns,int supported,const HaloSenseTransform *live,
                                     const HaloSenseTransform *other,HaloSenseTransform *out) {
    HaloSenseTransform eased=*live;
    if(halo_sense_grip_continuity()){
        HaloSenseGripInput in={.session=input->session,.generation=input->generation,.epoch=epoch,.epoch_ns=epoch_ns,
            .unit=datum,.weapon=weapon,.supported=supported,.live=live,.other=other};
        if(!sense_grip_present(&sense_grip,&in,&eased))eased=*live;
    }else sense_grip_reset(&sense_grip);
    int zoom=-1;if(sense_action_for_unit(unit,datum,weapon,&zoom)!=HALO_SENSE_ACTION_SCOPE)zoom=-1;
    HaloSenseScopeInputPose scope={.session=input->session,.generation=input->generation,.epoch_ns=epoch_ns,
        .unit=datum,.weapon=weapon,.enabled=halo_sense_scope_smoothing(),.zoom=zoom,.camera=&sense_camera,.accepted=&eased};
    if(!sense_scope_filter(&sense_scope_aim,&scope,out))*out=eased;
}
