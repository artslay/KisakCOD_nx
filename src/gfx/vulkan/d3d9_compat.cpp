            msg, sizeof(msg),
            "[KisakCOD][VK DRAW] fail=indices prim=%u count=%u indices=%p buffer=%p start=%u base=%d\n",
            primitiveType, primitiveCount,
            static_cast<void *>(m_indices),
            m_indices ? m_indices->buffer : nullptr,
            startIndex, baseVertexIndex);
        Switch_LogWrite(msg);
        return E_FAIL;