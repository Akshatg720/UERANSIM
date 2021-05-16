//
// This file is a part of UERANSIM open source project.
// Copyright (c) 2021 ALİ GÜNGÖR.
//
// The software and all associated files are licensed under GPL-3.0
// and subject to the terms and conditions defined in LICENSE file.
//

#include "mm.hpp"
#include <algorithm>
#include <lib/nas/utils.hpp>
#include <ue/app/task.hpp>
#include <ue/nas/sm/sm.hpp>
#include <ue/rrc/task.hpp>

namespace nr::ue
{

void NasMm::performPlmnSelection()
{
    // TODO
}

void NasMm::handleServingCellChange(const UeCellInfo &servingCell)
{
    if (m_cmState == ECmState::CM_CONNECTED)
    {
        m_logger->err("Serving cell change in CM-CONNECTED");
        return;
    }

    if (servingCell.cellCategory != ECellCategory::ACCEPTABLE_CELL &&
        servingCell.cellCategory != ECellCategory::SUITABLE_CELL)
    {
        m_logger->err("Serving cell change with unhandled cell category");
        return;
    }

    m_logger->info("Serving cell determined [%s]", servingCell.gnbName.c_str());

    if (m_mmState == EMmState::MM_REGISTERED || m_mmState == EMmState::MM_DEREGISTERED)
    {
        bool isSuitable = servingCell.cellCategory == ECellCategory::SUITABLE_CELL;

        if (m_mmState == EMmState::MM_REGISTERED)
            switchMmState(EMmState::MM_REGISTERED, isSuitable ? EMmSubState::MM_REGISTERED_NORMAL_SERVICE
                                                              : EMmSubState::MM_REGISTERED_LIMITED_SERVICE);
        else
            switchMmState(EMmState::MM_DEREGISTERED, isSuitable ? EMmSubState::MM_DEREGISTERED_NORMAL_SERVICE
                                                                : EMmSubState::MM_DEREGISTERED_LIMITED_SERVICE);
    }
    // todo: else, other states abnormal case

    resetRegAttemptCounter();

    m_usim->m_servingCell = servingCell;
    m_usim->m_currentPlmn = servingCell.cellId.plmn;
    m_usim->m_currentTai =
        nas::VTrackingAreaIdentity{nas::utils::PlmnFrom(servingCell.cellId.plmn), octet3{servingCell.tac}};
}

void NasMm::handleRrcConnectionSetup()
{
    switchCmState(ECmState::CM_CONNECTED);
}

void NasMm::handleRrcConnectionRelease()
{
    switchCmState(ECmState::CM_IDLE);
}

void NasMm::handleRadioLinkFailure()
{
    if (m_cmState == ECmState::CM_CONNECTED)
    {
        m_logger->err("Radio link failure detected");
    }

    m_usim->m_servingCell = std::nullopt;
    m_usim->m_currentPlmn = std::nullopt;
    m_usim->m_currentTai = std::nullopt;

    handleRrcConnectionRelease();

    if (m_mmState == EMmState::MM_REGISTERED)
        switchMmState(EMmState::MM_REGISTERED, EMmSubState::MM_REGISTERED_NA);
    else if (m_mmState == EMmState::MM_DEREGISTERED)
        switchMmState(EMmState::MM_DEREGISTERED, EMmSubState::MM_DEREGISTERED_NA);
}

void NasMm::localReleaseConnection()
{
    m_logger->info("Performing local release of NAS connection");

    m_base->rrcTask->push(new NwUeNasToRrc(NwUeNasToRrc::LOCAL_RELEASE_CONNECTION));
}

void NasMm::handlePaging(const std::vector<GutiMobileIdentity> &tmsiIds)
{
    if (m_usim->m_storedGuti.type == nas::EIdentityType::NO_IDENTITY)
        return;
    bool tmsiMatches = false;
    for (auto &tmsi : tmsiIds)
    {
        if (tmsi.amfSetId == m_usim->m_storedGuti.gutiOrTmsi.amfSetId &&
            tmsi.amfPointer == m_usim->m_storedGuti.gutiOrTmsi.amfPointer &&
            tmsi.tmsi == m_usim->m_storedGuti.gutiOrTmsi.tmsi)
        {
            tmsiMatches = true;
            break;
        }
    }

    if (!tmsiMatches)
        return;

    m_timers->t3346.stop();

    if (m_mmState == EMmState::MM_REGISTERED_INITIATED || m_mmState == EMmState::MM_DEREGISTERED_INITIATED ||
        m_mmState == EMmState::MM_SERVICE_REQUEST_INITIATED)
    {
        m_logger->debug("Ignoring received Paging, another procedure already initiated");
        return;
    }

    m_logger->debug("Responding to received Paging");

    if (m_cmState == ECmState::CM_CONNECTED)
        sendMobilityRegistration(ERegUpdateCause::PAGING_OR_NOTIFICATION);
    else
        sendServiceRequest(EServiceReqCause::IDLE_PAGING);
}

} // namespace nr::ue