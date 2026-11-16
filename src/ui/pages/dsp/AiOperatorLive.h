#pragma once

#include "AiOperatorFacade.h"
#include "core/Session.h"

namespace fa::ui::dsp
{
/** Real binding of the AI operator panel to Session::useAiOperator / AiDspController / ApiKeyStore.
    Used in Live mode (the demo facade is used in Demo mode). All callbacks fire on the message thread. */
std::unique_ptr<AiOperatorFacade> createLiveAiOperator (Session& session, std::function<void*()> windowHandle);
} // namespace fa::ui::dsp
